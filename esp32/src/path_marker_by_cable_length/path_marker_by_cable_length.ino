// =============================================================================
// SpiderCam — path_marker
//
// A standalone "teach pendant" for building inspection routes by hand. The
// operator jogs each of the 4 cable motors with hold-to-move buttons to position
// the central gantry at a waypoint, then SAVEs that pose. Each waypoint is one line:
//
//     M1:105.3,M2:98.7,M3:112.1,M4:101.5      (wire paid out per motor, cm)
//
// --- Waypoint storage lives on the Pi, not the ESP32 -------------------------
// Waypoints are NOT stored on ESP32 flash. SAVE / DELETE LAST / list-on-load are
// forwarded by this firmware (as an HTTP client) to a small Python server on the
// Raspberry Pi (waypoint_server.py, port 8765), which owns waypoints.txt. The
// browser still talks only to the ESP32 (/save, /deletelast, /waypoints); those
// handlers proxy to the Pi and relay its JSON back. If the Pi is unreachable the
// ESP32 replies 502 so the UI can show a clear "server not reachable" banner. The
// Pi address is PI_SERVER below, but we prefer resolving it by mDNS at boot.
//
// This is a completely separate interface from inspection_lift.ino / argus.html —
// it does NOT use the Argus command link. Browse to http://spidercam.local/ (or
// the board's DHCP IP) and the UI is served at /pathmarker.
//
// Foundation reused verbatim from the proven sketches:
//   • inspection_lift.ino  — WiFi STA + DHCP + mDNS, the Task-WDT fix, the brownout
//                            disable, and the DRV8825 STEP/DIR pin map + STEPS_PER_CM.
//   • v2_1_manual_jog.ino   — the single-step DRV8825 pulse (DIR, STEP HIGH, delay,
//                            STEP LOW, delay).
//
// --- Communication model (why HTTP and not WebSockets) -----------------------
// inspection_lift uses the synchronous WebServer over plain HTTP — there is no
// WebSocket code anywhere in this project and no WS library installed. Rather
// than add a dependency, path_marker reuses that exact proven pattern: hold-to-
// move is driven by short HTTP commands that set a per-motor direction flag, and
// the main loop interleaves server.handleClient() with one motor step at a time,
// so an incoming STOP is serviced within a single step (~22 ms) — responsive
// enough for jogging. A deadman timer (see DEADMAN_MS) auto-stops any motor whose
// browser stops refreshing the command, so a dropped network or closed tab can
// never leave a cable winding unattended.
//
// --- Always-energised motors --------------------------------------------------
// The DRV8825 SLEEP/RESET (HIGH = awake) and ENABLE (LOW = active) lines are
// hardwired on this board — inspection_lift and v2_1 drive only STEP/DIR and
// never define an enable/sleep GPIO. So the drivers hold full current as long as
// they are powered. path_marker keeps it that way: it never disables a driver and
// has NO idle-off / current-reduction timer, so every motor holds its position
// indefinitely. (If an ENABLE/SLEEP GPIO is ever wired, drive ENABLE LOW and
// SLEEP HIGH in setup(); none exists in the current wiring.)
// =============================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <math.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_task_wdt.h"
#include "secrets.h"            // defines WIFI_SSID / WIFI_PASS (gitignored)

WebServer server(80);

// ── Stepper config (pin map + calibration verbatim from inspection_lift) ──────
// Motor 1 = STEP 23 / DIR 22, Motor 2 = 17 / 16, Motor 3 = 18 / 5, Motor 4 = 21 / 19.
const int STEP_PINS[4] = { 23, 17, 18, 21 };
const int DIR_PINS[4]  = { 22, 16,  5, 19 };

// Per-motor direction invert. Set true for any motor wound opposite the software
// convention (a WIND command must reel cable IN — shorten the wire — on every motor).
const bool DIR_INVERT[4] = { false, false, false, false };

const float STEPS_PER_CM = 15.92f;   // calibrated: 200 steps/rev / cm-per-rotation

// ── Geometry references ───────────────────────────────────────────────────────
const float START_WIRE_CM   = 118.0f;  // boot assumption: each cable paid out 118 cm
const float INSPECT_Z_CM     = 90.0f;   // reference inspection height (all motors equal)

// ── Jog speed ─────────────────────────────────────────────────────────────────
// Per-step pulse delay applied to every manual jog move. Each step is two delays
// (HIGH + LOW), so step rate ≈ 1 / (2 × delay); larger delay = slower. This is the
// inspection_lift RAISE_STEP_DELAY_US value, mandated for all jogging here.
const unsigned int JOG_STEP_DELAY_US = 7200;   // raise_step_delay_us
const float        CM_PER_STEP       = 1.0f / STEPS_PER_CM;

// ── Typed-move & Z-lock limits ────────────────────────────────────────────────
// A typed "go to N cm" target is clamped to this range purely to catch fat-finger
// input (e.g. 950 instead of 95.0) before it crashes the gantry — widen it if your
// rig legitimately needs lengths outside it. Z-lock single-press step counts are
// capped the same way (coarse = 500; the cap leaves headroom but blocks runaways).
const float MOVE_MIN_CM     = 10.0f;
const float MOVE_MAX_CM     = 400.0f;
const long  ZLOCK_MAX_STEPS = 5000;

// ── Deadman / safety ──────────────────────────────────────────────────────────
// A motor only keeps jogging while its command is being refreshed. If no WIND/
// UNWIND for this motor arrives within DEADMAN_MS, it stops on its own. The UI
// re-sends the command every ~150 ms while a button is held, well inside this.
const uint32_t DEADMAN_MS = 700;

// ── Reachability: DHCP + mDNS (verbatim from inspection_lift) ─────────────────
const char *MDNS_HOST = "spidercam";        // -> spidercam.local

// ── Pi waypoint server ────────────────────────────────────────────────────────
// PI_SERVER is the fallback base URL used as-is. But this hotspot's DHCP is not
// fixed (the same reason the ESP32 itself is reached by mDNS), so at boot we try
// to resolve the Pi's mDNS host to its current IP and use that instead; the
// resolved base lands in piBase. Update PI_SERVER's IP if the Pi's lease changes
// AND mDNS is unavailable.
const char    *PI_SERVER    = "http://192.168.253.249:8765";  // fallback — Pi's current DHCP IP
const char    *PI_MDNS_HOST = "raspberrypiOMV";               // Pi advertises raspberrypiOMV.local (avahi)
const uint16_t PI_PORT      = 8765;
String         piBase;                                        // resolved base URL, set by resolvePiServer()

// ── Live motor state ──────────────────────────────────────────────────────────
// Wire paid out per motor (cm). Winding shortens the wire (cm DECREASES); unwinding
// lengthens it (cm INCREASES). Boot assumes every cable starts at START_WIRE_CM —
// match this physically before powering on.
float    motorCm[4]   = { START_WIRE_CM, START_WIRE_CM, START_WIRE_CM, START_WIRE_CM };
int8_t   jogDir[4]    = { 0, 0, 0, 0 };     // +1 = wind (shorten), -1 = unwind (lengthen), 0 = stop
uint32_t lastCmdMs[4] = { 0, 0, 0, 0 };     // millis() of the last command that set jogDir[i]

// ── Automated motion: RESET and PLAY ─────────────────────────────────────────
// RESET (all motors → 118 cm) and PLAY (step through saved waypoints) both drive
// all 4 motors to per-motor targets at once, via a Bresenham scheme so every motor
// finishes together. Like jog, a move advances ONE master tick per loop()
// iteration, so handleClient() runs between ticks (STOP honoured within ~22 ms)
// and motorCm is updated every step — an interrupted move leaves positions
// reflecting exactly where the motors stopped. Progress is polled by the UI via
// /positions (this project has no WebSocket; see the communication-model note).
// OP_MOVE covers a typed single-motor "go to N cm" and a Z-lock shift — both are
// just coordinated moves with no post-completion action (unlike RESET / PLAY).
enum Op { OP_NONE, OP_RESET, OP_PLAY, OP_MOVE };
Op     currentOp    = OP_NONE;

bool   moveActive   = false;       // a coordinated move is in flight
long   mvAbs[4]     = { 0, 0, 0, 0 };   // |steps| each motor takes this move
long   mvAcc[4]     = { 0, 0, 0, 0 };   // Bresenham accumulators
long   mvMax        = 0;            // master tick count (max of mvAbs)
long   mvT          = 0;            // master ticks completed
float  mvCmStep[4]  = { 0, 0, 0, 0 };   // cm applied per fired step (sign = direction)
float  mvTarget[4]  = { 0, 0, 0, 0 };   // exact target cm (snapped on completion)

const int MAX_WAYPOINTS = 64;
float  playWp[MAX_WAYPOINTS][4];   // route targets fetched from the Pi for PLAY
int    playTotal    = 0;           // waypoints in the loaded route
int    playIdx      = 0;           // index currently being approached (0-based)
bool   playActive   = false;       // PLAY sequence in progress

// =============================================================================
// Motor stepping — one coordinated step of all currently-active motors.
// Called once per loop() iteration so server.handleClient() runs between steps
// and a STOP is honoured immediately. All active motors share JOG_STEP_DELAY_US,
// so holding several buttons (or WIND ALL) moves them at the same rate.
// =============================================================================
void stepActiveMotorsOnce() {
    uint32_t now = millis();
    bool active[4];
    bool any = false;

    for (int i = 0; i < 4; i++) {
        if (jogDir[i] != 0 && (now - lastCmdMs[i]) >= DEADMAN_MS) {
            jogDir[i] = 0;                  // deadman: command went stale, stop this motor
        }
        active[i] = (jogDir[i] != 0);
        if (active[i]) any = true;
    }
    if (!any) return;                       // nothing to do; drivers still hold position

    // Direction: WIND (+1) reels cable in. Matches inspection_lift's convention
    // where positive steps wind in → DIR forward (before any per-motor invert).
    for (int i = 0; i < 4; i++) {
        if (!active[i]) continue;
        bool fwd = (jogDir[i] > 0) ^ DIR_INVERT[i];
        digitalWrite(DIR_PINS[i], fwd ? HIGH : LOW);
    }
    delayMicroseconds(20);                  // DIR setup before the pulse

    for (int i = 0; i < 4; i++) if (active[i]) digitalWrite(STEP_PINS[i], HIGH);
    delayMicroseconds(JOG_STEP_DELAY_US);
    for (int i = 0; i < 4; i++) if (active[i]) digitalWrite(STEP_PINS[i], LOW);
    delayMicroseconds(JOG_STEP_DELAY_US);

    for (int i = 0; i < 4; i++) {
        if (!active[i]) continue;
        motorCm[i] += (jogDir[i] > 0) ? -CM_PER_STEP : +CM_PER_STEP;   // wind shortens, unwind lengthens
    }
}

// =============================================================================
// Waypoint storage — proxied to the Pi waypoint server over HTTP.
// =============================================================================
// Resolve the Pi server base URL. Prefer the Pi's current mDNS-resolved IP (DHCP
// on this hotspot is not fixed); fall back to the hardcoded PI_SERVER if mDNS
// can't answer. Safe to call repeatedly (boot, reconnect, after a failed proxy).
static void resolvePiServer() {
    IPAddress ip = MDNS.queryHost(PI_MDNS_HOST, 2000);
    if ((uint32_t)ip != 0) {
        piBase = String("http://") + ip.toString() + ":" + PI_PORT;
        Serial.printf("[WP] Pi server via mDNS: %s (%s.local)\n", piBase.c_str(), PI_MDNS_HOST);
    } else {
        piBase = PI_SERVER;
        Serial.printf("[WP] Pi mDNS lookup failed; using fallback %s\n", piBase.c_str());
    }
}

// One request to the Pi waypoint server. Returns the HTTP status (>0) and fills
// `out` with the body, or returns <=0 on transport failure (and re-resolves the
// Pi, whose IP may have changed). Used by the browser relays (proxyToPi) and by
// PLAY (fetch the route) and RESET (clear it). Blocks loop() up to ~2 s on
// failure — fine: these are operator actions that never happen mid-jog.
static int piRequest(bool post, const char *path, const String &body, String &out) {
    if (piBase.length() == 0) resolvePiServer();

    WiFiClient client;
    HTTPClient http;
    String url = piBase + path;
    if (!http.begin(client, url)) return -1;
    http.setConnectTimeout(1500);
    http.setTimeout(2000);

    int code;
    if (post) {
        http.addHeader("Content-Type", "application/json");
        code = http.POST(body);
    } else {
        code = http.GET();
    }
    if (code > 0) out = http.getString();
    http.end();

    if (code > 0) {
        Serial.printf("[WP] %s %s -> %d\n", post ? "POST" : "GET", path, code);
    } else {
        Serial.printf("[WP] %s %s FAILED (%s) — re-resolving Pi\n",
                      post ? "POST" : "GET", path, HTTPClient::errorToString(code).c_str());
        resolvePiServer();
    }
    return code;
}

// Relay a Pi request straight back to the browser (save / delete / list). On any
// failure reply 502 {"error":"pi_unreachable"} so the UI can show its banner.
static void proxyToPi(bool post, const char *path, const String &body) {
    String resp;
    int code = piRequest(post, path, body, resp);
    if (code > 0) server.send(code, "application/json", resp);
    else          server.send(502, "application/json", "{\"error\":\"pi_unreachable\"}");
}

// =============================================================================
// Automated coordinated moves (RESET / PLAY) — Bresenham, one tick per loop().
// =============================================================================
// Arm a coordinated move of all 4 motors to `target` (cm each): set DIR pins and
// the Bresenham state. Leaves moveActive false if nothing needs to move.
void startMoveTo(const float target[4]) {
    mvMax = 0;
    mvT   = 0;
    for (int i = 0; i < 4; i++) {
        float deltaCm = motorCm[i] - target[i];            // >0 ⇒ shorten (wind in)
        long  steps   = lroundf(deltaCm * STEPS_PER_CM);
        bool  wind    = (steps > 0);
        mvAbs[i]    = labs(steps);
        mvAcc[i]    = 0;
        mvCmStep[i] = wind ? -CM_PER_STEP : +CM_PER_STEP;  // wind shortens, unwind lengthens
        mvTarget[i] = target[i];
        bool fwd = wind ^ DIR_INVERT[i];                   // same DIR convention as jog
        digitalWrite(DIR_PINS[i], fwd ? HIGH : LOW);
        if (mvAbs[i] > mvMax) mvMax = mvAbs[i];
    }
    delayMicroseconds(20);                                 // DIR setup before first pulse
    moveActive = (mvMax > 0);                              // already on target ⇒ nothing to do
}

// Arm a coordinated move by explicit per-motor STEP counts (sign sets direction:
// >0 winds/shortens the wire, <0 unwinds/lengthens it). Used by typed single-motor
// moves and Z-lock. Reuses the SAME Bresenham tick (stepCoordinatedMoveOnce), so
// motorCm is updated only on confirmed steps — never by a timer or assumed completion.
void startMoveSteps(const long stepsReq[4]) {
    mvMax = 0;
    mvT   = 0;
    for (int i = 0; i < 4; i++) {
        bool wind   = (stepsReq[i] > 0);
        mvAbs[i]    = labs(stepsReq[i]);
        mvAcc[i]    = 0;
        mvCmStep[i] = wind ? -CM_PER_STEP : +CM_PER_STEP;     // wind shortens, unwind lengthens
        mvTarget[i] = motorCm[i] + mvAbs[i] * mvCmStep[i];    // exact end cm (snapped on completion)
        bool fwd = wind ^ DIR_INVERT[i];                      // same DIR convention as jog
        digitalWrite(DIR_PINS[i], fwd ? HIGH : LOW);
        if (mvAbs[i] > mvMax) mvMax = mvAbs[i];
    }
    delayMicroseconds(20);                                    // DIR setup before first pulse
    moveActive = (mvMax > 0);
}

// Advance the in-flight move by one master tick (fire the motors due this tick).
// Returns true once the move is complete (all steps taken).
bool stepCoordinatedMoveOnce() {
    bool fire[4];
    for (int i = 0; i < 4; i++) {
        fire[i] = false;
        if (mvAbs[i] == 0) continue;
        mvAcc[i] += mvAbs[i];
        if (mvAcc[i] >= mvMax) { mvAcc[i] -= mvMax; fire[i] = true; }
    }
    for (int i = 0; i < 4; i++) if (fire[i]) digitalWrite(STEP_PINS[i], HIGH);
    delayMicroseconds(JOG_STEP_DELAY_US);
    for (int i = 0; i < 4; i++) if (fire[i]) digitalWrite(STEP_PINS[i], LOW);
    delayMicroseconds(JOG_STEP_DELAY_US);
    for (int i = 0; i < 4; i++) if (fire[i]) motorCm[i] += mvCmStep[i];

    if (++mvT >= mvMax) {
        for (int i = 0; i < 4; i++) if (mvAbs[i] > 0) motorCm[i] = mvTarget[i];   // snap exact
        return true;
    }
    return false;
}

// Carry the current operation forward when a coordinated move finishes.
void onMoveComplete() {
    if (currentOp == OP_RESET) {
        Serial.println("[RESET] motors at 118.0 cm — clearing waypoints on Pi");
        String resp;
        piRequest(true, "/waypoint/reset", "", resp);      // best-effort clear
        currentOp = OP_NONE;
    } else if (currentOp == OP_PLAY) {
        Serial.printf("[PLAY] reached waypoint %d/%d\n", playIdx + 1, playTotal);
        playIdx++;
        if (playIdx >= playTotal) {
            Serial.println("[PLAY] route complete");
            playActive = false;
            currentOp  = OP_NONE;
        }
        // else: loop() will start the move to the next waypoint
    } else if (currentOp == OP_MOVE) {
        Serial.println("[MOVE] target reached");
        currentOp = OP_NONE;
    }
}

// Parse {"waypoints":[{"M1":..,"M2":..,"M3":..,"M4":..}, ...]} into out[][4].
// Returns the count parsed (capped at maxN). Tolerant of whitespace after colons.
int parseWaypointList(const String &json, float out[][4], int maxN) {
    int n = 0, p = 0;
    while (n < maxN) {
        int i1 = json.indexOf("\"M1\":", p); if (i1 < 0) break;
        int i2 = json.indexOf("\"M2\":", i1);
        int i3 = json.indexOf("\"M3\":", i2);
        int i4 = json.indexOf("\"M4\":", i3);
        if (i2 < 0 || i3 < 0 || i4 < 0) break;
        out[n][0] = atof(json.c_str() + i1 + 5);           // +5 skips the "Mx": key
        out[n][1] = atof(json.c_str() + i2 + 5);
        out[n][2] = atof(json.c_str() + i3 + 5);
        out[n][3] = atof(json.c_str() + i4 + 5);
        n++;
        p = i4 + 5;
    }
    return n;
}

// =============================================================================
// HTTP handlers
// =============================================================================
extern const char PATHMARKER_HTML[] PROGMEM;

static void handlePage() {
    server.send_P(200, "text/html", PATHMARKER_HTML);
}

// Live state for the UI: positions plus PLAY/RESET progress (the UI polls this).
static void sendPositions() {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"m\":[%.1f,%.1f,%.1f,%.1f],"
             "\"play\":{\"active\":%s,\"index\":%d,\"total\":%d},"
             "\"reset\":{\"active\":%s},"
             "\"move\":{\"active\":%s}}",
             motorCm[0], motorCm[1], motorCm[2], motorCm[3],
             playActive ? "true" : "false", playIdx, playTotal,
             (currentOp == OP_RESET) ? "true" : "false",
             (currentOp == OP_MOVE)  ? "true" : "false");
    server.send(200, "application/json", buf);
}

// Parse the ?m= argument: "all" → -1 (every motor), "1".."4" → 0-based index, else -2 (invalid).
static int parseMotorArg() {
    if (!server.hasArg("m")) return -2;
    String m = server.arg("m");
    if (m == "all") return -1;
    int n = m.toInt();
    if (n >= 1 && n <= 4) return n - 1;
    return -2;
}

// /jog?m=<1-4|all>&dir=<wind|unwind>  — start OR refresh (keepalive) a hold-to-move.
static void handleJog() {
    if (moveActive || currentOp != OP_NONE) {       // jog is locked out during RESET / PLAY
        server.send(409, "application/json", "{\"error\":\"busy\"}");
        return;
    }
    int idx = parseMotorArg();
    String dir = server.arg("dir");
    int8_t d = (dir == "wind") ? +1 : (dir == "unwind") ? -1 : 0;
    if (idx == -2 || d == 0) {
        server.send(400, "application/json", "{\"error\":\"bad args\"}");
        return;
    }
    uint32_t now = millis();
    if (idx == -1) {
        for (int i = 0; i < 4; i++) { jogDir[i] = d; lastCmdMs[i] = now; }
    } else {
        jogDir[idx] = d; lastCmdMs[idx] = now;
    }
    sendPositions();
}

// /jogstop?m=<1-4|all>  — button released: stop immediately.
static void handleJogStop() {
    int idx = parseMotorArg();
    if (idx == -2) { server.send(400, "application/json", "{\"error\":\"bad args\"}"); return; }
    if (idx == -1) { for (int i = 0; i < 4; i++) jogDir[i] = 0; }
    else           { jogDir[idx] = 0; }
    sendPositions();
}

static void handlePositions() { sendPositions(); }

// Page load → ask the Pi for the route, relay {"waypoints":[...]} to the browser.
static void handleWaypoints() { proxyToPi(false, "/waypoint/list", ""); }

// SAVE → POST the current pose to the Pi as JSON; relay {"status":"ok","total":n}.
static void handleSave() {
    char body[80];
    snprintf(body, sizeof(body), "{\"M1\":%.1f,\"M2\":%.1f,\"M3\":%.1f,\"M4\":%.1f}",
             motorCm[0], motorCm[1], motorCm[2], motorCm[3]);
    proxyToPi(true, "/waypoint/save", String(body));
}

// DELETE LAST → POST (no body) to the Pi; relay {"status":"ok","total":n}.
static void handleDeleteLast() { proxyToPi(true, "/waypoint/delete_last", ""); }

// /reset — move all 4 motors to 118 cm together, then clear the Pi's waypoint
// file. Returns immediately ({"status":"resetting"}); the move runs in loop() and
// the UI watches reset.active in /positions to know when it's done.
static void handleReset() {
    if (moveActive || currentOp != OP_NONE) { server.send(409, "application/json", "{\"error\":\"busy\"}"); return; }
    for (int i = 0; i < 4; i++) jogDir[i] = 0;          // cancel any jog
    float target[4] = { START_WIRE_CM, START_WIRE_CM, START_WIRE_CM, START_WIRE_CM };
    currentOp = OP_RESET;
    Serial.println("[RESET] moving all motors to 118.0 cm");
    startMoveTo(target);
    if (!moveActive) onMoveComplete();                  // already at 118 ⇒ clear Pi now
    server.send(200, "application/json", "{\"status\":\"resetting\"}");
}

// /play — fetch the route from the Pi and start playing it back. Returns
// {"status":"playing","total":n}; progress is reported via /positions.
static void handlePlay() {
    if (moveActive || currentOp != OP_NONE) { server.send(409, "application/json", "{\"error\":\"busy\"}"); return; }
    String resp;
    int code = piRequest(false, "/waypoint/list", "", resp);
    if (code <= 0) { server.send(502, "application/json", "{\"error\":\"pi_unreachable\"}"); return; }
    playTotal = parseWaypointList(resp, playWp, MAX_WAYPOINTS);
    if (playTotal == 0) { server.send(400, "application/json", "{\"error\":\"no_waypoints\"}"); return; }
    for (int i = 0; i < 4; i++) jogDir[i] = 0;          // cancel any jog
    playIdx    = 0;
    playActive = true;
    currentOp  = OP_PLAY;
    Serial.printf("[PLAY] starting route of %d waypoints\n", playTotal);
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"status\":\"playing\",\"total\":%d}", playTotal);
    server.send(200, "application/json", buf);
}

// /stopplay — interrupt PLAY (or RESET) immediately; motors hold where they are,
// and motorCm already reflects the true stopped position.
static void handleStopPlay() {
    moveActive = false;
    playActive = false;
    currentOp  = OP_NONE;
    Serial.println("[PLAY/RESET] stopped by operator");
    sendPositions();
}

// /move?m=<1-4>&cm=<value> — drive ONE motor to an exact wire length using the
// coordinated-move engine (so tracking stays per-step). Single motor only ("all"
// rejected); value clamped to [MOVE_MIN_CM, MOVE_MAX_CM]. Locked out during any
// other move (409 busy). The UI watches move.active in /positions to know when done.
static void handleMove() {
    if (moveActive || currentOp != OP_NONE) { server.send(409, "application/json", "{\"error\":\"busy\"}"); return; }
    int idx = parseMotorArg();
    if (idx < 0) { server.send(400, "application/json", "{\"error\":\"bad motor\"}"); return; }
    if (!server.hasArg("cm")) { server.send(400, "application/json", "{\"error\":\"no_cm\"}"); return; }
    float cm = server.arg("cm").toFloat();
    if (isnan(cm) || cm < MOVE_MIN_CM || cm > MOVE_MAX_CM) {
        server.send(400, "application/json", "{\"error\":\"out_of_range\"}");
        return;
    }
    for (int i = 0; i < 4; i++) jogDir[i] = 0;              // cancel any jog
    float target[4];
    for (int i = 0; i < 4; i++) target[i] = motorCm[i];     // hold the other three
    target[idx] = cm;
    currentOp = OP_MOVE;
    Serial.printf("[MOVE] M%d -> %.1f cm\n", idx + 1, cm);
    startMoveTo(target);
    if (!moveActive) currentOp = OP_NONE;                   // already at target
    sendPositions();
}

// /zlock?d=<xp|xm|yp|ym>&steps=<N> — shift the gantry in X or Y while holding Z.
// Opposite motor pairs move equal-and-opposite so the 4-motor average (Z) is
// unchanged. All four motors take the same |N| steps, so via the coordinated engine
// they fire every tick — simultaneous and equal. Locked out during any other move.
static void handleZlock() {
    if (moveActive || currentOp != OP_NONE) { server.send(409, "application/json", "{\"error\":\"busy\"}"); return; }
    String d   = server.arg("d");
    long  steps = server.arg("steps").toInt();
    if (steps <= 0 || steps > ZLOCK_MAX_STEPS) { server.send(400, "application/json", "{\"error\":\"bad_steps\"}"); return; }
    long s[4];
    // wind (+) shortens that wire; unwind (-) lengthens. Each pattern sums to zero
    // across the four motors, so Z (the average wire length) is preserved exactly.
    if      (d == "xp") { s[0]=+steps; s[1]=+steps; s[2]=-steps; s[3]=-steps; }  // X+: wind M1,M2 / unwind M3,M4
    else if (d == "xm") { s[0]=-steps; s[1]=-steps; s[2]=+steps; s[3]=+steps; }  // X-
    else if (d == "yp") { s[0]=+steps; s[1]=-steps; s[2]=-steps; s[3]=+steps; }  // Y+: wind M1,M4 / unwind M2,M3
    else if (d == "ym") { s[0]=-steps; s[1]=+steps; s[2]=+steps; s[3]=-steps; }  // Y-
    else { server.send(400, "application/json", "{\"error\":\"bad_dir\"}"); return; }
    for (int i = 0; i < 4; i++) jogDir[i] = 0;              // cancel any jog
    currentOp = OP_MOVE;
    Serial.printf("[ZLOCK] %s %ld steps\n", d.c_str(), steps);
    startMoveSteps(s);
    if (!moveActive) currentOp = OP_NONE;
    sendPositions();
}

static void handleNotFound() {
    Serial.printf("[HTTP] 404 %s\n", server.uri().c_str());
    server.send(404, "application/json", "{\"status\":\"not_found\"}");
}

// ── WiFi (verbatim from inspection_lift) ──────────────────────────────────────
static void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);                    // keep the radio responsive for HTTP
    WiFi.begin(WIFI_SSID, WIFI_PASS);        // DHCP — address assigned by the hotspot
    Serial.printf("[WiFi] Connecting to \"%s\"", WIFI_SSID);

    unsigned long lastRetry = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - lastRetry > 10000) {
            Serial.print("[retry]");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            lastRetry = millis();
        }
    }
    Serial.printf("\n[WiFi] Connected! IP: %s  GW: %s  Mask: %s\n",
        WiFi.localIP().toString().c_str(),
        WiFi.gatewayIP().toString().c_str(),
        WiFi.subnetMask().toString().c_str());

    MDNS.end();
    if (MDNS.begin(MDNS_HOST)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] reachable at http://%s.local/\n", MDNS_HOST);
    } else {
        Serial.println("[mDNS] start failed (use the DHCP IP instead)");
    }
}

// ── setup / loop ──────────────────────────────────────────────────────────────
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // disable brownout detector (WiFi current spike)

    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[boot] path_marker starting...");

    // Stepper pins idle. Drivers are hardwired enabled, so they hold current the
    // moment the pins go output-LOW — and we never disable them after this.
    for (int i = 0; i < 4; i++) {
        pinMode(STEP_PINS[i], OUTPUT);
        pinMode(DIR_PINS[i],  OUTPUT);
        digitalWrite(STEP_PINS[i], LOW);
        digitalWrite(DIR_PINS[i],  LOW);
        motorCm[i] = START_WIRE_CM;
    }
    Serial.printf("[boot] assuming all cables at %.1f cm (inspection ref z = %.1f cm)\n",
                  START_WIRE_CM, INSPECT_Z_CM);

    // Task WDT fix: WiFi starves Core 0's idle task and the WDT would fire
    // TG0WDT_SYS_RESET. Watch no idle task (idle_core_mask = 0). (memory: esp32-wifi-wdt-fix)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,
        .trigger_panic = false,
    };
    esp_task_wdt_reconfigure(&wdt_config);

    connectWiFi();
    resolvePiServer();                           // locate the Pi waypoint server (mDNS, else fallback)

    server.enableCORS(true);
    server.on("/",            HTTP_GET, handlePage);
    server.on("/pathmarker",  HTTP_GET, handlePage);
    server.on("/jog",         HTTP_GET, handleJog);          // start / keepalive a hold-to-move
    server.on("/jogstop",     HTTP_GET, handleJogStop);      // button released
    server.on("/positions",   HTTP_GET, handlePositions);    // live cm display
    server.on("/move",        HTTP_GET, handleMove);         // typed: drive one motor to exact cm
    server.on("/zlock",       HTTP_GET, handleZlock);        // Z-lock X/Y shift (Z held)
    server.on("/waypoints",   HTTP_GET, handleWaypoints);    // list (page load)
    server.on("/save",        HTTP_GET, handleSave);         // save current pose
    server.on("/deletelast",  HTTP_GET, handleDeleteLast);   // remove last pose
    server.on("/reset",       HTTP_GET, handleReset);        // all motors → 118 cm + clear route
    server.on("/play",        HTTP_GET, handlePlay);         // play saved route
    server.on("/stopplay",    HTTP_GET, handleStopPlay);     // interrupt play/reset
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.printf("[HTTP] Server ready — open http://%s/pathmarker\n",
                  WiFi.localIP().toString().c_str());
}

void loop() {
    server.handleClient();        // service one pending command

    if (moveActive) {
        // An automated (RESET/PLAY) move is running: advance it one tick.
        if (stepCoordinatedMoveOnce()) { moveActive = false; onMoveComplete(); }
    } else if (currentOp == OP_PLAY && playActive) {
        // Between waypoints: kick off the move to the next one.
        startMoveTo(playWp[playIdx]);
        if (!moveActive) onMoveComplete();    // zero-distance waypoint ⇒ advance immediately
    } else {
        stepActiveMotorsOnce();   // manual jog (one coordinated step of held motors)
    }

    // If WiFi drops, reconnect so the pendant recovers on its own.
    static bool wasConnected = true;
    if (WiFi.status() != WL_CONNECTED) {
        if (wasConnected) { Serial.println("[WiFi] Lost connection — reconnecting..."); wasConnected = false; }
        // Safety: stop ALL motion if the link is gone — the operator can neither
        // release a jog button nor send STOP_PLAY.
        for (int i = 0; i < 4; i++) jogDir[i] = 0;
        moveActive = false; playActive = false; currentOp = OP_NONE;
        connectWiFi();
        resolvePiServer();        // the Pi's IP may have changed across the outage
        wasConnected = true;
    }
}

// =============================================================================
// Embedded UI — served at /pathmarker. Self-contained, no external assets.
// =============================================================================
const char PATHMARKER_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>SpiderCam — Path Marker</title>
<style>
  :root { --bg:#0f1216; --panel:#181d24; --line:#283039; --txt:#e6edf3; --muted:#8b97a3;
          --wind:#1f6feb; --unwind:#d29922; --save:#238636; --del:#da3633; --accent:#2ea043;
          --go:#1f6feb; --xax:#3457b2; --yax:#2f7d57; }
  * { box-sizing:border-box; -webkit-tap-highlight-color:transparent; }
  body { margin:0; font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
         background:var(--bg); color:var(--txt); padding:14px; }
  h1 { font-size:18px; margin:0 0 2px; }
  .sub { color:var(--muted); font-size:12px; margin:0 0 14px; }
  .grid { display:grid; grid-template-columns:repeat(2,1fr); gap:12px; }
  @media (max-width:560px){ .grid { grid-template-columns:1fr; } }
  .card { background:var(--panel); border:1px solid var(--line); border-radius:12px; padding:14px; }
  .mhead { display:flex; justify-content:space-between; align-items:baseline; margin-bottom:10px; }
  .mname { font-weight:600; font-size:15px; }
  .mcm { font-variant-numeric:tabular-nums; font-size:22px; font-weight:700; }
  .mcm .u { font-size:12px; color:var(--muted); font-weight:500; margin-left:3px; }
  .row { display:flex; gap:10px; }
  button { flex:1; border:0; border-radius:10px; color:#fff; font-size:15px; font-weight:600;
           padding:16px 8px; cursor:pointer; touch-action:none; user-select:none; transition:filter .08s, transform .04s; }
  button:active { transform:translateY(1px); }
  .wind   { background:var(--wind); }
  .unwind { background:var(--unwind); }
  button.holding { filter:brightness(1.45); box-shadow:0 0 0 3px rgba(255,255,255,.25) inset; }
  /* typed length input + GO */
  .inrow { display:flex; gap:8px; align-items:center; }
  input.cmin { flex:1; min-width:0; padding:13px 10px; border-radius:10px; border:1px solid var(--line);
               background:#0d1117; color:var(--txt); font-size:17px; font-variant-numeric:tabular-nums; }
  input.cmin:focus { outline:2px solid var(--go); border-color:var(--go); }
  .gobtn { flex:0 0 auto; background:var(--go); padding:13px 20px; }
  .mv { color:var(--accent); font-size:12px; font-weight:600; margin-top:8px; min-height:14px; display:none; }
  .mv.on { display:block; }
  .mv.on::before { content:'⟳ '; }
  /* Z-lock */
  .zcard { margin-top:12px; }
  .zhead { display:flex; justify-content:space-between; align-items:baseline; font-weight:600; font-size:15px; margin-bottom:12px; }
  #zval { color:var(--accent); font-variant-numeric:tabular-nums; font-size:14px; font-weight:700; }
  .zstep { display:flex; gap:14px; flex-wrap:wrap; align-items:center; margin-bottom:14px; font-size:13px; }
  .zstep > span { color:var(--muted); }
  .zstep label { display:inline-flex; gap:5px; align-items:center; }
  .zpad { display:grid; grid-template-columns:repeat(3,1fr); gap:8px; }
  .zpad button { background:var(--xax); }
  .zpad button.zy { background:var(--yax); }
  #zYp{ grid-column:2; grid-row:1; } #zXm{ grid-column:1; grid-row:2; }
  #zXp{ grid-column:3; grid-row:2; } #zYm{ grid-column:2; grid-row:3; }
  /* collapsible manual jog */
  details.jogd { margin-top:12px; padding:0; }
  details.jogd > summary { cursor:pointer; padding:14px; font-weight:600; font-size:15px; list-style:none; }
  details.jogd > summary::-webkit-details-marker { display:none; }
  details.jogd > summary::before { content:'▸ '; color:var(--muted); }
  details.jogd[open] > summary::before { content:'▾ '; }
  details.jogd .jinner { padding:0 14px 14px; }
  .allrow { display:flex; gap:10px; margin-top:12px; }
  /* waypoints + shared */
  .allcard .mname { display:block; margin-bottom:10px; }
  .wpcard { margin-top:12px; }
  .wprow { display:flex; gap:10px; margin-bottom:10px; }
  .save { background:var(--save); }
  .del  { background:var(--del); }
  .estop{ background:#30363d; }
  .play { background:var(--accent); }
  .stopplay { background:var(--del); }
  .reset { background:var(--del); }
  button:disabled { opacity:.38; filter:grayscale(.3); cursor:not-allowed; transform:none; }
  input:disabled { opacity:.5; }
  ol#wplist { margin:0; padding-left:22px; max-height:240px; overflow:auto; }
  ol#wplist li { font-variant-numeric:tabular-nums; font-size:14px; padding:4px 6px;
                 border-bottom:1px solid var(--line); border-radius:6px; }
  ol#wplist li.current { background:rgba(46,160,67,.22); font-weight:700; color:#fff; }
  .empty { color:var(--muted); font-size:13px; font-style:italic; }
  .stat { color:var(--muted); font-size:11px; margin-top:8px; min-height:14px; }
  .wphead { font-weight:600; font-size:14px; margin-bottom:8px; }
  #wpcount { color:var(--muted); font-weight:500; }
  #banner { display:none; background:#7d1f1f; border:1px solid var(--del); color:#fff;
            padding:11px 13px; border-radius:10px; margin-bottom:12px; font-weight:600; font-size:14px; }
</style>
</head>
<body>
  <h1>SpiderCam — Path Marker</h1>
  <p class="sub">Type a target length and hit GO, or use Z-Lock to shift X/Y while holding height. Hold-to-jog is under Manual Jog. Save a waypoint at each pose.</p>

  <div id="banner">⚠ Waypoint server not reachable — check Pi</div>

  <!-- per-motor: live length + typed target -->
  <div class="grid" id="motors"></div>

  <!-- Z-lock: move opposite pairs together to shift X/Y without changing Z -->
  <div class="card zcard">
    <div class="zhead"><span>Z-Lock Movement</span><span id="zval">Z: --.- cm</span></div>
    <div class="zstep">
      <span>Step:</span>
      <label><input type="radio" name="zstep" value="50">Fine (50)</label>
      <label><input type="radio" name="zstep" value="200" checked>Medium (200)</label>
      <label><input type="radio" name="zstep" value="500">Coarse (500)</label>
    </div>
    <div class="zpad">
      <button class="zy" id="zYp" data-z>Y+</button>
      <button id="zXm" data-z>X−</button>
      <button id="zXp" data-z>X+</button>
      <button class="zy" id="zYm" data-z>Y−</button>
    </div>
  </div>

  <!-- existing hold-to-move jog, tucked away as a fallback -->
  <details class="card jogd">
    <summary>Manual Jog (fine tension)</summary>
    <div class="jinner">
      <div class="grid" id="jogmotors"></div>
      <div class="allrow">
        <button class="wind"   data-m="all" data-dir="wind">WIND ALL</button>
        <button class="unwind" data-m="all" data-dir="unwind">UNWIND ALL</button>
      </div>
    </div>
  </details>

  <div class="card wpcard">
    <div class="wprow">
      <button class="save" id="btnSave">SAVE WAYPOINT</button>
      <button class="del"  id="btnDel">DELETE LAST</button>
    </div>
    <div class="wprow">
      <button class="play" id="btnPlay" disabled>▶ PLAY ROUTE</button>
      <button class="stopplay" id="btnStopPlay" style="display:none;">■ STOP</button>
    </div>
    <div class="wprow">
      <button class="estop" id="btnStopAll">STOP ALL MOTORS</button>
      <button class="reset" id="btnReset">⟲ RESET → 118 cm</button>
    </div>
    <div class="wphead">Saved waypoints <span id="wpcount">(0)</span></div>
    <ol id="wplist"></ol>
    <div class="empty" id="wpempty">No waypoints saved yet.</div>
    <div class="stat" id="stat"></div>
  </div>

<script>
const MOVE_MIN = 10, MOVE_MAX = 400;   // mirrors firmware MOVE_MIN_CM / MOVE_MAX_CM

// ---- build the 4 motor cards (length readout + typed target + GO) ------------
const motorsEl = document.getElementById('motors');
for (let m = 1; m <= 4; m++) {
  const c = document.createElement('div');
  c.className = 'card';
  c.innerHTML =
    '<div class="mhead"><span class="mname">Motor ' + m + '</span>' +
    '<span class="mcm"><span id="cm' + m + '">--.-</span><span class="u">cm</span></span></div>' +
    '<div class="inrow">' +
      '<input class="cmin" id="inp' + m + '" data-inp="' + m + '" type="text" inputmode="decimal" autocomplete="off">' +
      '<button class="gobtn" data-go="' + m + '">GO</button>' +
    '</div>' +
    '<div class="mv" id="mv' + m + '">moving…</div>';
  motorsEl.appendChild(c);
}

// ---- build the 4 manual-jog cards (hold WIND / UNWIND) -----------------------
const jogEl = document.getElementById('jogmotors');
for (let m = 1; m <= 4; m++) {
  const c = document.createElement('div');
  c.className = 'card';
  c.innerHTML =
    '<div class="mhead"><span class="mname">Motor ' + m + '</span></div>' +
    '<div class="row">' +
      '<button class="wind"   data-m="' + m + '" data-dir="wind">WIND</button>' +
      '<button class="unwind" data-m="' + m + '" data-dir="unwind">UNWIND</button>' +
    '</div>';
  jogEl.appendChild(c);
}

const stat = document.getElementById('stat');
function setStat(t){ stat.textContent = t; }

// ---- live state: positions + PLAY/RESET/MOVE progress ------------------------
let wpCount = 0;                 // saved waypoints (drives PLAY-enabled)
let locked = false;              // controls disabled during RESET / PLAY / MOVE
let prevPlay = false, prevReset = false, prevMove = false;

function highlightWp(index){
  document.querySelectorAll('#wplist li').forEach((li,i) => li.classList.toggle('current', i === index));
}
function markMoving(m, on){ const el = document.getElementById('mv' + m); if (el) el.classList.toggle('on', on); }
function clearMoving(){ for (let m = 1; m <= 4; m++) markMoving(m, false); }

// Enable/disable controls. isLocked → block jog/typed-move/z-lock/save/delete/
// reset/play. showStop → reveal the STOP button (the one way to abort a move).
function setLocked(isLocked, showStop){
  locked = isLocked;
  document.querySelectorAll('button[data-m]').forEach(b => b.disabled = isLocked);   // jog
  document.querySelectorAll('button[data-go]').forEach(b => b.disabled = isLocked);  // GO
  document.querySelectorAll('input[data-inp]').forEach(b => b.disabled = isLocked);  // typed inputs
  document.querySelectorAll('button[data-z]').forEach(b => b.disabled = isLocked);   // z-lock
  document.getElementById('btnSave').disabled = isLocked;
  document.getElementById('btnDel').disabled = isLocked;
  document.getElementById('btnReset').disabled = isLocked;
  document.getElementById('btnStopAll').disabled = isLocked;
  document.getElementById('btnPlay').disabled = isLocked || wpCount === 0;
  document.getElementById('btnStopPlay').style.display = showStop ? '' : 'none';
}

function applyState(j){
  if (!j || !j.m) return;        // ignore non-state replies (e.g. a 409 {"error":"busy"})
  for (let i = 0; i < 4; i++){
    const el = document.getElementById('cm' + (i+1));
    if (el) el.textContent = j.m[i].toFixed(1);
    // typed input tracks the live length unless the operator is editing it
    const inp = document.getElementById('inp' + (i+1));
    if (inp && document.activeElement !== inp) inp.value = j.m[i].toFixed(1);
  }
  // Z = average of all four wire lengths (held constant by Z-lock moves)
  const z = (j.m[0] + j.m[1] + j.m[2] + j.m[3]) / 4;
  const zv = document.getElementById('zval');
  if (zv) zv.textContent = 'Z: ' + z.toFixed(1) + ' cm';

  const play  = j.play  || {active:false, index:0, total:0};
  const reset = j.reset || {active:false};
  const move  = j.move  || {active:false};

  // RESET lifecycle
  if (reset.active && !prevReset) setStat('Resetting to 118 cm...');
  if (!reset.active && prevReset){ setStat('Reset complete — all motors at 118.0 cm'); loadWaypoints(); }

  // PLAY lifecycle
  if (play.active){
    setStat('Playing — waypoint ' + (play.index + 1) + ' of ' + play.total);
    highlightWp(play.index);
  } else if (prevPlay){
    setStat('Route complete');
    highlightWp(-1);
    loadWaypoints();
  }

  // MOVE lifecycle (typed go-to + Z-lock)
  if (!move.active && prevMove){ setStat('Move complete'); clearMoving(); }

  prevPlay = play.active; prevReset = reset.active; prevMove = move.active;
  setLocked(play.active || reset.active || move.active, play.active || move.active);
}
async function pollState(){
  try { const r = await fetch('/positions'); applyState(await r.json()); }
  catch(e){ /* transient; next tick retries */ }
}
setInterval(pollState, 150);
pollState();

// ---- fetch helper ------------------------------------------------------------
function showBanner(show){ document.getElementById('banner').style.display = show ? 'block' : 'none'; }
function fmt(v){ return (v == null || isNaN(v)) ? '?' : Number(v).toFixed(1); }

// fetch JSON from an ESP32 endpoint; throw if the request failed or the ESP32
// reported the Pi is unreachable (HTTP 502 {"error":"pi_unreachable"}).
async function jget(url, opts){
  const r = await fetch(url, opts);
  const j = await r.json().catch(() => ({}));
  if (!r.ok || (j && j.error)) throw new Error((j && j.error) || ('HTTP ' + r.status));
  return j;
}

// ---- typed length move -------------------------------------------------------
async function sendMove(m){
  if (locked) return;
  const inp = document.getElementById('inp' + m);
  const v = parseFloat(inp.value);
  if (isNaN(v)){ setStat('Enter a number for M' + m); return; }
  if (v < MOVE_MIN || v > MOVE_MAX){ setStat('M' + m + ': out of range (' + MOVE_MIN + '–' + MOVE_MAX + ' cm)'); return; }
  markMoving(m, true);                 // immediate feedback; poll confirms within 150 ms
  inp.blur();
  try {
    await jget('/move?m=' + m + '&cm=' + v.toFixed(1));
    setStat('M' + m + ' → ' + v.toFixed(1) + ' cm…');
  } catch(e){
    markMoving(m, false);
    const msg = String(e.message);
    if (msg.indexOf('busy') >= 0) setStat('busy — a move is already running');
    else if (msg.indexOf('out_of_range') >= 0) setStat('M' + m + ': value out of range');
    else setStat('move failed');
  }
}
document.querySelectorAll('button[data-go]').forEach(b =>
  b.addEventListener('click', () => sendMove(b.dataset.go)));
document.querySelectorAll('input[data-inp]').forEach(inp =>
  inp.addEventListener('keydown', e => { if (e.key === 'Enter'){ e.preventDefault(); sendMove(inp.dataset.inp); } }));

// ---- Z-lock ------------------------------------------------------------------
function zStep(){ const r = document.querySelector('input[name=zstep]:checked'); return r ? parseInt(r.value, 10) : 200; }
async function sendZlock(d){
  if (locked) return;
  const n = zStep();
  try { await jget('/zlock?d=' + d + '&steps=' + n); setStat('Z-lock ' + d.toUpperCase() + ' — ' + n + ' steps…'); }
  catch(e){ if (String(e.message).indexOf('busy') >= 0) setStat('busy — wait for current move'); else setStat('z-lock failed'); }
}
[['zXp','xp'],['zXm','xm'],['zYp','yp'],['zYm','ym']].forEach(([id,d]) =>
  document.getElementById(id).addEventListener('click', () => sendZlock(d)));

// ---- hold-to-move (manual jog) -----------------------------------------------
// pointerdown → send jog + start a 150 ms keepalive (refreshes the firmware
// deadman). pointerup/leave/cancel → stop the keepalive + send jogstop.
async function sendJog(m, dir){
  try { const r = await fetch('/jog?m=' + m + '&dir=' + dir); applyState(await r.json()); }
  catch(e){ /* keepalive will retry */ }
}
async function sendStop(m){
  try { const r = await fetch('/jogstop?m=' + m); applyState(await r.json()); }
  catch(e){ setStat('stop send failed — deadman will halt motor'); }
}
function bindHold(btn){
  const m = btn.dataset.m, dir = btn.dataset.dir;
  let ka = null;
  const start = (ev) => {
    ev.preventDefault();
    if (ka || locked) return;          // no jogging during RESET / PLAY / MOVE
    btn.classList.add('holding');
    sendJog(m, dir);
    ka = setInterval(() => sendJog(m, dir), 150);
  };
  const end = () => {
    if (!ka) return;
    clearInterval(ka); ka = null;
    btn.classList.remove('holding');
    sendStop(m);
  };
  btn.addEventListener('pointerdown',   start);
  btn.addEventListener('pointerup',     end);
  btn.addEventListener('pointerleave',  end);
  btn.addEventListener('pointercancel', end);
}
document.querySelectorAll('button[data-m]').forEach(bindHold);

// ---- waypoints (stored on the Pi; the ESP32 proxies to it) -------------------
function renderWaypoints(data){
  const wps = (data && data.waypoints) || [];
  wpCount = wps.length;
  const list = document.getElementById('wplist');
  const empty = document.getElementById('wpempty');
  document.getElementById('wpcount').textContent = '(' + wps.length + ')';
  list.innerHTML = '';
  empty.style.display = wps.length ? 'none' : 'block';
  for (const w of wps){
    const li = document.createElement('li');
    li.textContent = 'M1:' + fmt(w.M1) + '  M2:' + fmt(w.M2) + '  M3:' + fmt(w.M3) + '  M4:' + fmt(w.M4);
    list.appendChild(li);
  }
  // PLAY needs at least one waypoint (unless a move has the controls locked).
  if (!locked) document.getElementById('btnPlay').disabled = (wpCount === 0);
}
async function loadWaypoints(){
  try { renderWaypoints(await jget('/waypoints')); showBanner(false); }
  catch(e){ showBanner(true); setStat('could not load waypoints from Pi'); }
}
document.getElementById('btnSave').addEventListener('click', async () => {
  try {
    const j = await jget('/save');
    setStat('waypoint saved (total ' + j.total + ')');
    showBanner(false);
    await loadWaypoints();
  } catch(e){ showBanner(true); setStat('save failed — Pi server unreachable'); }
});
document.getElementById('btnDel').addEventListener('click', async () => {
  try {
    const j = await jget('/deletelast');
    setStat('last waypoint deleted (total ' + j.total + ')');
    showBanner(false);
    await loadWaypoints();
  } catch(e){ showBanner(true); setStat('delete failed — Pi server unreachable'); }
});
document.getElementById('btnStopAll').addEventListener('click', () => {
  document.querySelectorAll('button.holding').forEach(b => b.classList.remove('holding'));
  sendStop('all'); setStat('all motors stopped');
});

// ---- play route --------------------------------------------------------------
document.getElementById('btnPlay').addEventListener('click', async () => {
  if (locked || wpCount === 0) return;
  try {
    const j = await jget('/play');
    showBanner(false);
    setStat('playing route — ' + j.total + ' waypoints...');
  } catch(e){
    if (String(e.message).indexOf('no_waypoints') >= 0) setStat('no waypoints to play');
    else { showBanner(true); setStat('play failed — Pi unreachable'); }
  }
});
document.getElementById('btnStopPlay').addEventListener('click', async () => {
  try { await fetch('/stopplay'); setStat('stopped'); }
  catch(e){ setStat('stop send failed'); }
});

// ---- reset -------------------------------------------------------------------
document.getElementById('btnReset').addEventListener('click', async () => {
  if (locked) return;
  if (!confirm('This will move all motors to 118 cm and clear all saved waypoints. Are you sure?')) return;
  try { await jget('/reset'); showBanner(false); setStat('Resetting to 118 cm...'); }
  catch(e){ showBanner(true); setStat('reset failed — Pi unreachable'); }
});

loadWaypoints();
</script>
</body>
</html>)HTMLPAGE";
