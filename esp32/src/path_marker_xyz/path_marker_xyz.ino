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

// ── Coordinated-move velocity profile (trapezoidal accel/decel) ───────────────
// RESET / PLAY / typed go-to / Z-lock / HOME / GOTO moves ramp the master-tick
// rate so the wire no longer jerks from jumping straight to full speed. The delay
// is per pulse-phase (applied to both STEP HIGH and STEP LOW), so step period
// = 2 × delay; a smaller delay = faster. The profile: accelerate over the first
// RAMP_STEPS ticks (STEP_DELAY_START → STEP_DELAY_MIN), cruise at STEP_DELAY_MIN,
// decelerate over the last RAMP_STEPS ticks. All retune-friendly named constants.
// (Jog stays at the constant JOG_STEP_DELAY_US — it has no fixed endpoint to ramp
// against.) See moveStepDelayUs().
const int RAMP_STEPS        = 200;     // master ticks to accelerate / decelerate over
const int STEP_DELAY_START  = 18000;   // µs/phase — slow start & end of every move
const int STEP_DELAY_MIN    = 7200;    // µs/phase — top cruise speed (was 4000; retune after testing)

// ── Typed-move & Z-lock limits ────────────────────────────────────────────────
// A typed "go to N cm" target is clamped to this range purely to catch fat-finger
// input (e.g. 950 instead of 95.0) before it crashes the gantry — widen it if your
// rig legitimately needs lengths outside it. Z-lock single-press step counts are
// capped the same way (coarse = 500; the cap leaves headroom but blocks runaways).
const float MOVE_MIN_CM     = 10.0f;
const float MOVE_MAX_CM     = 400.0f;
const long  ZLOCK_MAX_STEPS = 5000;

// ── XYZ kinematics (this firmware only) ───────────────────────────────────────
// Origin (0,0,0) is the M1 pulley projected to the floor; X→M2, Y→M4, Z = height.
// Pulleys sit at 93 cm; cables attach to the central unit at ±10.5 cm from centre,
// at the unit's attach-point height (the tracked Z). Values are pre-calibrated.
const float PULLEY_POS[4][3] = {
    {   0.0f,   0.0f,  93.0f },   // M1 — bottom-left
    { 135.0f,   0.0f,  93.0f },   // M2 — bottom-right
    { 135.0f, 135.0f,  93.0f },   // M3 — top-right
    {   0.0f, 135.0f,  93.0f }    // M4 — top-left
};
const float ATTACH_OFFSET[4][2] = {
    { -10.5f, -10.5f },   // M1 — bottom-left of unit
    { +10.5f, -10.5f },   // M2 — bottom-right of unit
    { +10.5f, +10.5f },   // M3 — top-right of unit
    { -10.5f, +10.5f }    // M4 — top-left of unit
};

// Workspace reference ranges — ADVISORY ONLY. The firmware no longer blocks moves
// on these (the operator determines safe limits physically through testing); they
// are mirrored in the UI as `LIM` purely to highlight out-of-range inputs red.
// Cable-length validation has been removed entirely (no MIN_CABLE_CM/MAX_CABLE_CM).
const float MIN_X = 15.0f, MAX_X = 120.0f;   // frame edges (advisory)
const float MIN_Y = 15.0f, MAX_Y = 120.0f;
const float MIN_Z = 0.0f, MAX_Z = 80.0f;     // attach-point height above floor (advisory; HOME target is 0)

// IK: cable length for each motor given a target unit position (z = attach height).
void calcCableLengths(float x, float y, float z, float lengths[4]) {
    for (int i = 0; i < 4; i++) {
        float dx = PULLEY_POS[i][0] - (x + ATTACH_OFFSET[i][0]);
        float dy = PULLEY_POS[i][1] - (y + ATTACH_OFFSET[i][1]);
        float dz = PULLEY_POS[i][2] - z;
        lengths[i] = sqrtf(dx*dx + dy*dy + dz*dz);
    }
}

// FK: best-fit (x,y,z) for four measured cable lengths via Gauss-Newton least
// squares. Over-determined (4 eq, 3 unknowns) so it tolerates small stepper drift.
// Seeds from the workspace centre; round-trips to <0.001 cm on this geometry
// (verified offline). Used by SAVE to record the real-world pose. Result → out[3].
void calcXYZFromLengths(const float L[4], float out[3]) {
    float x = 67.5f, y = 67.5f, z = 50.0f;          // centre seed
    for (int it = 0; it < 25; it++) {
        float JTJ[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
        float JTr[3]    = {0,0,0};
        for (int i = 0; i < 4; i++) {
            float dx = PULLEY_POS[i][0] - (x + ATTACH_OFFSET[i][0]);
            float dy = PULLEY_POS[i][1] - (y + ATTACH_OFFSET[i][1]);
            float dz = PULLEY_POS[i][2] - z;
            float g  = sqrtf(dx*dx + dy*dy + dz*dz);
            if (g < 1e-4f) g = 1e-4f;
            float r  = g - L[i];
            float J[3] = { -dx/g, -dy/g, -dz/g };   // d g / d(x,y,z)
            for (int a = 0; a < 3; a++) {
                JTr[a] += J[a] * r;
                for (int b = 0; b < 3; b++) JTJ[a][b] += J[a] * J[b];
            }
        }
        // Solve JTJ . delta = -JTr  (3x3 Gaussian elimination, partial pivot).
        float M[3][4];
        for (int a = 0; a < 3; a++) { for (int b = 0; b < 3; b++) M[a][b] = JTJ[a][b]; M[a][3] = -JTr[a]; }
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            int piv = c;
            for (int r2 = c + 1; r2 < 3; r2++) if (fabsf(M[r2][c]) > fabsf(M[piv][c])) piv = r2;
            if (fabsf(M[piv][c]) < 1e-9f) { ok = false; break; }
            for (int k = 0; k < 4; k++) { float t = M[c][k]; M[c][k] = M[piv][k]; M[piv][k] = t; }
            for (int r2 = 0; r2 < 3; r2++) {
                if (r2 == c) continue;
                float f = M[r2][c] / M[c][c];
                for (int k = c; k < 4; k++) M[r2][k] -= f * M[c][k];
            }
        }
        if (!ok) break;
        float ddx = M[0][3] / M[0][0], ddy = M[1][3] / M[1][1], ddz = M[2][3] / M[2][2];
        x += ddx; y += ddy; z += ddz;
        if (fabsf(ddx) + fabsf(ddy) + fabsf(ddz) < 1e-5f) break;
    }
    out[0] = x; out[1] = y; out[2] = z;
}

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
enum Op { OP_NONE, OP_RESET, OP_PLAY, OP_MOVE, OP_PERIMETER };
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

// ── Perimeter inspection workflow (corners + auto route) ─────────────────────
// Four operator-taught corners, persisted on the Pi (corners.txt) and mirrored in
// RAM here. The inspection route is a 7-leg coordinated sequence:
//   raise above home → C1 → C2 → C3 → C4 → return above home → home
// reusing the same one-leg-per-loop() state machine as PLAY (see OP_PERIMETER).
struct Corner { float x, y, z; bool set; };
Corner corners[4] = {};            // corners[0] = Corner 1 … corners[3] = Corner 4

const float HOME_X = 67.5f, HOME_Y = 67.5f, HOME_Z = 0.0f;   // route's final "home" leg
const float ABOVE_HOME_Z = 30.0f;  // raise/return height above home (route legs 1 & 6)
const int   PERI_LEGS = 7;         // Z30, C1, C2, C3, C4, Z30, Home
// Step labels reported in /positions route.label (index = leg being approached);
// "Complete" is reported when idle. Kept in sync with the /pathmarker UI's PERI_LABELS.
const char *ROUTE_LABELS[PERI_LEGS] = {
    "Raising from home", "Moving to Corner 1", "Moving to Corner 2",
    "Moving to Corner 3", "Moving to Corner 4", "Returning above home", "Going home"
};
float  periRoute[PERI_LEGS][3];    // XYZ of each leg, built when the route starts
int    periTotal    = 0;           // legs in the active route (= PERI_LEGS)
int    periIdx      = 0;           // leg currently being approached (0-based)
bool   periActive   = false;       // perimeter route in progress

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

// Per-phase pulse delay for the master tick currently being fired (mvT of mvMax),
// following a trapezoidal profile. ramp = RAMP_STEPS normally; for short moves
// (mvMax < 2×RAMP_STEPS) it shrinks to mvMax/2 so the two ramps meet at the
// midpoint with no cruise — accelerate to the middle, then decelerate, so a short
// move can never overshoot top speed. Because the delay gates the single shared
// master tick (not per motor), all 4 Bresenham motors stay in lockstep and finish
// together. Linear interpolation between STEP_DELAY_START (slow) and _MIN (fast).
static unsigned int moveStepDelayUs() {
    long total = mvMax;
    if (total <= 1) return STEP_DELAY_MIN;
    long ramp = RAMP_STEPS;
    if (total < 2L * RAMP_STEPS) ramp = total / 2;
    if (ramp < 1) return STEP_DELAY_MIN;

    long idx  = mvT;                      // 0-based index of the tick being fired now
    long fromEnd = total - 1 - idx;       // 0 on the final tick
    long span = (long)STEP_DELAY_START - (long)STEP_DELAY_MIN;
    if (idx < ramp)     return (unsigned int)(STEP_DELAY_START - span * idx     / ramp);  // ramp up
    if (fromEnd < ramp) return (unsigned int)(STEP_DELAY_START - span * fromEnd / ramp);  // ramp down
    return STEP_DELAY_MIN;                                                                 // cruise
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
    unsigned int d = moveStepDelayUs();        // trapezoidal accel/decel for this tick
    for (int i = 0; i < 4; i++) if (fire[i]) digitalWrite(STEP_PINS[i], HIGH);
    delayMicroseconds(d);
    for (int i = 0; i < 4; i++) if (fire[i]) digitalWrite(STEP_PINS[i], LOW);
    delayMicroseconds(d);
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
    } else if (currentOp == OP_PERIMETER) {
        Serial.printf("[PERIMETER] reached leg %d/%d\n", periIdx + 1, periTotal);
        periIdx++;
        if (periIdx >= periTotal) {
            Serial.println("[PERIMETER] route complete");
            periActive = false;
            currentOp  = OP_NONE;
        }
        // else: loop() will start the move to the next leg
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
// Perimeter-corner persistence — proxied to the Pi waypoint server.
// =============================================================================
// Corners live in RAM (corners[]) but are mirrored to the Pi (corners.txt) so they
// survive page refreshes and ESP32 reboots. We POST the full set on every save/clear
// and pull it back on boot. The Pi keeps exactly 4 records keyed by "index" (0..3).

// Parse the Pi's [{"index":0,"x":..,"y":..,"z":..,"set":true}, ...] into corners[].
// Tolerant of whitespace/key order; each record is placed by its "index" field, and
// each field is only accepted if it falls inside that record's object. Returns the
// number of records applied.
static int parseCornersResponse(const String &json) {
    int applied = 0, p = 0;
    while (true) {
        int ii = json.indexOf("\"index\":", p);
        if (ii < 0) break;
        int idx   = atoi(json.c_str() + ii + 8);
        int next  = json.indexOf("\"index\":", ii + 8);   // start of the following record
        int bound = (next < 0) ? (int)json.length() : next;

        int ix = json.indexOf("\"x\":",   ii);
        int iy = json.indexOf("\"y\":",   ii);
        int iz = json.indexOf("\"z\":",   ii);
        int is = json.indexOf("\"set\":", ii);
        if (idx >= 0 && idx < 4 &&
            ix > 0 && ix < bound && iy > 0 && iy < bound &&
            iz > 0 && iz < bound && is > 0 && is < bound) {
            corners[idx].x = atof(json.c_str() + ix + 4);   // +4 skips "x":
            corners[idx].y = atof(json.c_str() + iy + 4);
            corners[idx].z = atof(json.c_str() + iz + 4);
            const char *sv = json.c_str() + is + 6;          // +6 skips "set":
            while (*sv == ' ') sv++;
            corners[idx].set = (*sv == 't');                 // 't'rue vs 'f'alse
            applied++;
        }
        p = bound;
    }
    return applied;
}

// Boot: pull the saved corners from the Pi so they survive reboots/refreshes. On any
// failure we keep the all-unset RAM defaults and log it (the operator can re-teach).
static void loadCornersFromPi() {
    String resp;
    int code = piRequest(false, "/corners/load", "", resp);
    if (code != 200) {
        Serial.printf("[CORNER] load from Pi failed (HTTP %d) — starting with none\n", code);
        return;
    }
    int n = parseCornersResponse(resp);
    Serial.printf("[CORNER] loaded %d corner record(s) from Pi:\n", n);
    for (int i = 0; i < 4; i++) {
        if (corners[i].set)
            Serial.printf("  Corner %d: (%.1f, %.1f, %.1f)  set\n",
                          i + 1, corners[i].x, corners[i].y, corners[i].z);
        else
            Serial.printf("  Corner %d: not set\n", i + 1);
    }
}

// After any save/clear: push the full corners[] array to the Pi so corners.txt stays
// in sync. Best-effort — a failure just logs; RAM remains the source of truth this run.
static void pushCornersToPi() {
    char body[320];
    int n = snprintf(body, sizeof(body), "[");
    for (int i = 0; i < 4; i++) {
        n += snprintf(body + n, sizeof(body) - n,
                      "%s{\"index\":%d,\"x\":%.1f,\"y\":%.1f,\"z\":%.1f,\"set\":%s}",
                      i ? "," : "", i, corners[i].x, corners[i].y, corners[i].z,
                      corners[i].set ? "true" : "false");
    }
    snprintf(body + n, sizeof(body) - n, "]");
    String resp;
    int code = piRequest(true, "/corners/save", String(body), resp);
    if (code != 200) Serial.printf("[CORNER] push to Pi failed (HTTP %d)\n", code);
}

// =============================================================================
// HTTP handlers
// =============================================================================
extern const char PATHMARKER_HTML[] PROGMEM;

static void handlePage() {
    server.send_P(200, "text/html", PATHMARKER_HTML);
}

// Live state for the UI (polled): cable lengths, the FK-derived real-world X/Y/Z
// for the status bar, and PLAY / RESET / MOVE / PERIMETER progress. The UI has no
// WebSocket (synchronous WebServer only), so the perimeter progress bar + step
// label are driven from peri.{active,index,total} here.
static void sendPositions() {
    float xyz[3];
    calcXYZFromLengths(motorCm, xyz);                 // FK: live pose for the status bar
    // route.* is the 7-step inspection-route progress consumed by the demo UI; peri.*
    // is kept for the existing /pathmarker UI. Both are driven from the same state.
    const char *rlabel = (periActive && periIdx < PERI_LEGS) ? ROUTE_LABELS[periIdx] : "Complete";
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"m\":[%.1f,%.1f,%.1f,%.1f],"
             "\"xyz\":[%.1f,%.1f,%.1f],"
             "\"play\":{\"active\":%s,\"index\":%d,\"total\":%d},"
             "\"reset\":{\"active\":%s},"
             "\"move\":{\"active\":%s},"
             "\"peri\":{\"active\":%s,\"index\":%d,\"total\":%d},"
             "\"route\":{\"active\":%s,\"step\":%d,\"total\":%d,\"label\":\"%s\"}}",
             motorCm[0], motorCm[1], motorCm[2], motorCm[3],
             xyz[0], xyz[1], xyz[2],
             playActive ? "true" : "false", playIdx, playTotal,
             (currentOp == OP_RESET) ? "true" : "false",
             (currentOp == OP_MOVE)  ? "true" : "false",
             periActive ? "true" : "false", periIdx, periTotal,
             periActive ? "true" : "false", periActive ? periIdx + 1 : 0, PERI_LEGS, rlabel);
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
// In the XYZ firmware we also derive the real-world (X,Y,Z) from the live cable
// lengths (FK) and store it alongside, so the route records coordinates too.
static void handleSave() {
    float xyz[3];
    calcXYZFromLengths(motorCm, xyz);
    char body[160];
    snprintf(body, sizeof(body),
             "{\"M1\":%.1f,\"M2\":%.1f,\"M3\":%.1f,\"M4\":%.1f,\"X\":%.1f,\"Y\":%.1f,\"Z\":%.1f}",
             motorCm[0], motorCm[1], motorCm[2], motorCm[3], xyz[0], xyz[1], xyz[2]);
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

// /stopplay — interrupt PLAY, RESET or the PERIMETER route immediately; motors
// hold where they are, and motorCm already reflects the true stopped position.
static void handleStopPlay() {
    moveActive = false;
    playActive = false;
    periActive = false;
    currentOp  = OP_NONE;
    Serial.println("[PLAY/RESET/PERIMETER] stopped by operator");
    sendPositions();
}

// /abort — stop ALL motion immediately (same as /stopplay), then command a
// background move to home (centre, Z=0). Returns at once; the home move runs in
// loop() as an OP_MOVE and the UI watches move.active to know when it has arrived.
static void handleAbort() {
    moveActive = false;
    playActive = false;
    periActive = false;
    for (int i = 0; i < 4; i++) jogDir[i] = 0;          // cancel any jog
    float L[4];
    calcCableLengths(HOME_X, HOME_Y, HOME_Z, L);        // IK for home (67.5, 67.5, 0)
    currentOp = OP_MOVE;
    startMoveTo(L);
    if (!moveActive) currentOp = OP_NONE;               // already at home
    Serial.println("[ABORT] stopped; returning home (67.5,67.5,0)");
    server.send(200, "application/json", "{\"status\":\"aborting\",\"home\":[67.5,67.5,0.0]}");
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

// /goto?x=&y=&z=  — GOTO_XYZ. No range validation: cable-length limits were removed
// and X/Y/Z limits are now advisory (UI-only red highlight); the operator determines
// safe limits physically. Compute the four IK cable lengths and arm a coordinated
// move to them. Only guards are 409 (another move running) and 400 (missing args).
static void handleGoto() {
    if (moveActive || currentOp != OP_NONE) { server.send(409, "application/json", "{\"error\":\"busy\"}"); return; }
    if (!server.hasArg("x") || !server.hasArg("y") || !server.hasArg("z")) {
        server.send(400, "application/json", "{\"error\":\"need x,y,z\"}"); return;
    }
    float x = server.arg("x").toFloat();
    float y = server.arg("y").toFloat();
    float z = server.arg("z").toFloat();
    float L[4];
    calcCableLengths(x, y, z, L);
    for (int i = 0; i < 4; i++) jogDir[i] = 0;          // cancel any jog
    currentOp = OP_MOVE;
    Serial.printf("[GOTO] (%.1f,%.1f,%.1f) -> L[%.1f,%.1f,%.1f,%.1f]\n", x, y, z, L[0], L[1], L[2], L[3]);
    startMoveTo(L);
    if (!moveActive) currentOp = OP_NONE;               // already there
    sendPositions();
}

// =============================================================================
// Perimeter inspection workflow handlers — corners (RAM) + auto route.
// =============================================================================
// URL index is 0-based: ?index=0..3 maps to Corner 1..4.
static int parseCornerIndex() {
    if (!server.hasArg("index")) return -1;
    int i = server.arg("index").toInt();
    return (i >= 0 && i < 4) ? i : -1;
}

// {"corners":[{"set":..,"x":..,"y":..,"z":..} × 4]} — shared by save/clear/list.
static void sendCornerList() {
    char buf[320];
    int n = snprintf(buf, sizeof(buf), "{\"corners\":[");
    for (int i = 0; i < 4; i++) {
        n += snprintf(buf + n, sizeof(buf) - n,
                      "%s{\"set\":%s,\"x\":%.1f,\"y\":%.1f,\"z\":%.1f}",
                      i ? "," : "", corners[i].set ? "true" : "false",
                      corners[i].x, corners[i].y, corners[i].z);
    }
    snprintf(buf + n, sizeof(buf) - n, "]}");
    server.send(200, "application/json", buf);
}

// /corner/save?index=0[&x=&y=&z=] — save from explicit XYZ, or (default) from the
// live gantry pose derived from the cable lengths via FK.
static void handleCornerSave() {
    int i = parseCornerIndex();
    if (i < 0) { server.send(400, "application/json", "{\"error\":\"bad index\"}"); return; }
    float x, y, z;
    if (server.hasArg("x") && server.hasArg("y") && server.hasArg("z")) {
        x = server.arg("x").toFloat(); y = server.arg("y").toFloat(); z = server.arg("z").toFloat();
    } else {
        float xyz[3]; calcXYZFromLengths(motorCm, xyz);
        x = xyz[0]; y = xyz[1]; z = xyz[2];
    }
    corners[i].x = x; corners[i].y = y; corners[i].z = z; corners[i].set = true;
    Serial.printf("[CORNER] saved %d -> (%.1f,%.1f,%.1f)\n", i + 1, x, y, z);
    pushCornersToPi();                  // persist to the Pi so it survives a reboot
    sendCornerList();
}

// /corner/clear?index=0 — clear a corner slot.
static void handleCornerClear() {
    int i = parseCornerIndex();
    if (i < 0) { server.send(400, "application/json", "{\"error\":\"bad index\"}"); return; }
    corners[i].set = false;
    Serial.printf("[CORNER] cleared %d\n", i + 1);
    pushCornersToPi();                  // persist to the Pi so it survives a reboot
    sendCornerList();
}

// /corner/list — all four corner slots.
static void handleCornerList() { sendCornerList(); }

// /corner/goto?index=0 — drive to a saved corner using the GOTO (IK) engine.
static void handleCornerGoto() {
    if (moveActive || currentOp != OP_NONE) { server.send(409, "application/json", "{\"error\":\"busy\"}"); return; }
    int i = parseCornerIndex();
    if (i < 0) { server.send(400, "application/json", "{\"error\":\"bad index\"}"); return; }
    if (!corners[i].set) { server.send(400, "application/json", "{\"error\":\"corner_not_set\"}"); return; }
    float L[4];
    calcCableLengths(corners[i].x, corners[i].y, corners[i].z, L);
    for (int k = 0; k < 4; k++) jogDir[k] = 0;
    currentOp = OP_MOVE;
    Serial.printf("[CORNER] goto %d -> (%.1f,%.1f,%.1f)\n", i + 1, corners[i].x, corners[i].y, corners[i].z);
    startMoveTo(L);
    if (!moveActive) currentOp = OP_NONE;
    sendPositions();
}

// /perimeter/play — run the inspection route: raise above home → C1 → C2 → C3 →
// C4 → return above home → home (7 legs). Corners are fetched fresh from the Pi
// (authoritative) at the start; 400 (listing which are missing) if any is unset.
// Reuses the OP_PERIMETER leg-at-a-time engine; progress is polled via /positions
// (route.* / peri.*), and /stopplay or /abort interrupts it.
static void handlePerimeterPlay() {
    if (moveActive || currentOp != OP_NONE) { server.send(409, "application/json", "{\"error\":\"busy\"}"); return; }

    // Pull the latest corners from the Pi before building the route. On a transient
    // failure, fall back to the corners already in RAM (boot-loaded / last fetch).
    String resp;
    int code = piRequest(false, "/corners/load", "", resp);
    if (code == 200) parseCornersResponse(resp);
    else Serial.printf("[PERIMETER] Pi corner fetch failed (HTTP %d) — using last-known corners\n", code);

    // Every corner must be set, else 400 listing which are missing (1-based).
    char miss[64]; int mlen = 0; bool any = false;
    for (int i = 0; i < 4; i++) if (!corners[i].set) {
        mlen += snprintf(miss + mlen, sizeof(miss) - mlen, "%s%d", any ? "," : "", i + 1);
        any = true;
    }
    if (any) {
        char err[96];
        snprintf(err, sizeof(err), "{\"error\":\"corners_not_set\",\"missing\":[%s]}", miss);
        server.send(400, "application/json", err); return;
    }

    // Build the 7-leg route: raise above home → 4 corners → return above home → home.
    periRoute[0][0] = HOME_X; periRoute[0][1] = HOME_Y; periRoute[0][2] = ABOVE_HOME_Z;   // leg 1
    for (int k = 0; k < 4; k++) {                                                          // legs 2-5
        periRoute[1 + k][0] = corners[k].x;
        periRoute[1 + k][1] = corners[k].y;
        periRoute[1 + k][2] = corners[k].z;
    }
    periRoute[5][0] = HOME_X; periRoute[5][1] = HOME_Y; periRoute[5][2] = ABOVE_HOME_Z;   // leg 6
    periRoute[6][0] = HOME_X; periRoute[6][1] = HOME_Y; periRoute[6][2] = HOME_Z;          // leg 7
    periTotal  = PERI_LEGS;
    periIdx    = 0;
    periActive = true;
    currentOp  = OP_PERIMETER;
    for (int i = 0; i < 4; i++) jogDir[i] = 0;
    Serial.println("[PERIMETER] starting route Z30->C1->C2->C3->C4->Z30->Home");
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"status\":\"running\",\"total\":%d}", periTotal);
    server.send(200, "application/json", buf);
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
    Serial.println("\n[boot] path_marker_xyz (IK/XYZ) starting...");

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
    loadCornersFromPi();                         // restore taught corners saved before the last reboot

    server.enableCORS(true);
    server.on("/",            HTTP_GET, handlePage);
    server.on("/pathmarker",  HTTP_GET, handlePage);
    server.on("/jog",         HTTP_GET, handleJog);          // start / keepalive a hold-to-move
    server.on("/jogstop",     HTTP_GET, handleJogStop);      // button released
    server.on("/positions",   HTTP_GET, handlePositions);    // live cm display
    server.on("/move",        HTTP_GET, handleMove);         // typed: drive one motor to exact cm
    server.on("/zlock",       HTTP_GET, handleZlock);        // Z-lock X/Y shift (Z held)
    server.on("/goto",        HTTP_GET, handleGoto);         // GOTO_XYZ via inverse kinematics
    server.on("/waypoints",   HTTP_GET, handleWaypoints);    // list (page load)
    server.on("/save",        HTTP_GET, handleSave);         // save current pose
    server.on("/deletelast",  HTTP_GET, handleDeleteLast);   // remove last pose
    server.on("/reset",       HTTP_GET, handleReset);        // all motors → 118 cm + clear route
    server.on("/play",        HTTP_GET, handlePlay);         // play saved route
    server.on("/stopplay",    HTTP_GET, handleStopPlay);     // interrupt play/reset/perimeter
    server.on("/abort",       HTTP_GET, handleAbort);        // stop everything + return home
    server.on("/corner/save",    HTTP_GET, handleCornerSave);     // save a perimeter corner (live pose or x/y/z)
    server.on("/corner/clear",   HTTP_GET, handleCornerClear);    // clear a corner slot
    server.on("/corner/list",    HTTP_GET, handleCornerList);     // all 4 corners
    server.on("/corner/goto",    HTTP_GET, handleCornerGoto);     // drive to a saved corner
    server.on("/perimeter/play", HTTP_GET, handlePerimeterPlay);  // run C1→C2→C3→C4→C1→Home
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
    } else if (currentOp == OP_PERIMETER && periActive) {
        // Between perimeter legs: convert the next leg's XYZ to cable lengths and go.
        float L[4];
        calcCableLengths(periRoute[periIdx][0], periRoute[periIdx][1], periRoute[periIdx][2], L);
        startMoveTo(L);
        if (!moveActive) onMoveComplete();    // zero-distance leg ⇒ advance immediately
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
<title>SpiderCam — Perimeter Inspection</title>
<style>
  :root { --bg:#0f1216; --panel:#181d24; --line:#283039; --txt:#e6edf3; --muted:#8b97a3;
          --wind:#1f6feb; --unwind:#d29922; --del:#da3633; --accent:#2ea043;
          --go:#1f6feb; --xax:#3457b2; --yax:#2f7d57; --set:#1b5e2a; }
  * { box-sizing:border-box; -webkit-tap-highlight-color:transparent; }
  body { margin:0; font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
         background:var(--bg); color:var(--txt); padding:14px; max-width:760px; margin:0 auto; }
  h1 { font-size:18px; margin:0 0 12px; }
  .card { background:var(--panel); border:1px solid var(--line); border-radius:12px; padding:14px; margin-bottom:12px; }
  .sechead { font-weight:600; font-size:15px; margin-bottom:12px; }
  button { border:0; border-radius:10px; color:#fff; font-size:15px; font-weight:600;
           padding:14px 10px; cursor:pointer; touch-action:none; user-select:none; transition:filter .08s, transform .04s; }
  button:active { transform:translateY(1px); }
  button:disabled { opacity:.38; filter:grayscale(.3); cursor:not-allowed; transform:none; }
  input:disabled { opacity:.5; }

  /* Section 1 — status bar */
  .statusbar { position:sticky; top:0; z-index:5; }
  .statline { display:flex; align-items:center; gap:14px; flex-wrap:wrap; font-variant-numeric:tabular-nums; }
  .statline + .statline { margin-top:10px; }
  .poslbl { color:var(--muted); font-size:12px; min-width:62px; }
  .statline b { color:var(--txt); font-weight:700; }
  .statline.cables { font-size:13px; color:var(--muted); }
  .homebtn { margin-left:auto; background:var(--go); font-weight:700; letter-spacing:.4px;
             padding:12px 22px; box-shadow:0 0 0 1px rgba(110,168,254,.35) inset; }

  /* Section 2 — go to position */
  .xyzrow { display:flex; gap:8px; align-items:flex-end; flex-wrap:wrap; margin-bottom:10px; }
  .xyzrow label { flex:1; min-width:70px; display:flex; flex-direction:column; gap:4px; font-size:12px; color:var(--muted); }
  input.xyzin { width:100%; padding:12px 10px; border-radius:10px; border:1px solid var(--line);
                background:#0d1117; color:var(--txt); font-size:17px; font-variant-numeric:tabular-nums; }
  input.xyzin:focus { outline:2px solid var(--go); border-color:var(--go); }
  input.xyzin.bad { border-color:var(--del); outline-color:var(--del); }
  .gotobtn { flex:0 0 auto; background:var(--accent); padding:12px 26px; }
  .calc { font-size:13px; color:var(--muted); display:flex; flex-wrap:wrap; gap:4px 14px; margin-bottom:6px; }
  .calc b { color:var(--txt); font-variant-numeric:tabular-nums; font-weight:700; }
  .gwarn { color:#e3b341; font-size:12px; font-weight:600; min-height:14px; margin-bottom:8px; }
  .prerow { display:flex; gap:10px; }
  .preset { flex:1; background:#30418b; font-size:14px; }

  /* Section 3 — perimeter corners */
  .cornergrid { display:grid; grid-template-columns:repeat(2,1fr); gap:10px; }
  @media (max-width:480px){ .cornergrid { grid-template-columns:1fr; } }
  .cornercard { border:1px solid var(--line); border-radius:10px; padding:12px; background:#21262d; }
  .cornercard.set { background:var(--set); border-color:#2ea043; }
  .cname { font-weight:700; font-size:14px; margin-bottom:6px; }
  .cxyz { font-size:13px; color:var(--txt); font-variant-numeric:tabular-nums; line-height:1.5; min-height:60px; }
  .cornercard:not(.set) .cxyz { color:var(--muted); font-style:italic; }
  .crow { display:flex; gap:8px; margin:8px 0; }
  .csave { flex:1; background:var(--accent); font-size:13px; padding:11px 6px; }
  .cgo { flex:0 0 auto; background:var(--go); font-size:13px; padding:11px 16px; }
  .cclear { width:100%; background:#30363d; font-size:12px; padding:9px 6px; }

  /* Section 4 — perimeter route */
  .routeline { font-size:13px; color:var(--muted); margin-bottom:12px; line-height:1.5; }
  .runbtn { width:100%; background:var(--accent); font-size:16px; font-weight:700; padding:16px; margin-bottom:8px; }
  .missing { color:#ff7b72; font-size:13px; font-weight:600; min-height:16px; }
  .progwrap { margin-top:12px; }
  .progbar { height:14px; border-radius:7px; background:#0d1117; border:1px solid var(--line); overflow:hidden; }
  .progfill { height:100%; width:0; background:var(--accent); transition:width .25s ease; }
  .progrow { display:flex; align-items:center; gap:12px; margin-top:10px; }
  .proglabel { flex:1; font-size:13px; font-weight:600; color:var(--txt); }
  .stopplay { flex:0 0 auto; background:var(--del); padding:12px 22px; }

  /* Section 5 — manual jog (collapsed) */
  details.jogd > summary { cursor:pointer; font-weight:600; font-size:15px; list-style:none; }
  details.jogd > summary::-webkit-details-marker { display:none; }
  details.jogd > summary::before { content:'▸ '; color:var(--muted); }
  details.jogd[open] > summary::before { content:'▾ '; }
  .jinner { padding-top:14px; }
  .zhead { display:flex; justify-content:space-between; align-items:baseline; font-weight:600; font-size:14px; margin-bottom:12px; }
  #zval { color:var(--accent); font-variant-numeric:tabular-nums; font-size:13px; font-weight:700; }
  .zstep { display:flex; gap:14px; flex-wrap:wrap; align-items:center; margin-bottom:14px; font-size:13px; }
  .zstep > span { color:var(--muted); }
  .zstep label { display:inline-flex; gap:5px; align-items:center; }
  .zpad { display:grid; grid-template-columns:repeat(3,1fr); gap:8px; margin-bottom:14px; }
  .zpad button { background:var(--xax); padding:16px 8px; }
  .zpad button.zy { background:var(--yax); }
  #zYp{ grid-column:2; grid-row:1; } #zXm{ grid-column:1; grid-row:2; }
  #zXp{ grid-column:3; grid-row:2; } #zYm{ grid-column:2; grid-row:3; }
  .grid { display:grid; grid-template-columns:repeat(2,1fr); gap:10px; }
  .jrow { display:flex; gap:8px; }
  .mname { font-weight:600; font-size:14px; margin-bottom:8px; display:block; }
  .wind { background:var(--wind); flex:1; }
  .unwind { background:var(--unwind); flex:1; }
  button.holding { filter:brightness(1.45); box-shadow:0 0 0 3px rgba(255,255,255,.25) inset; }
  .allrow { display:flex; gap:10px; margin:12px 0; }
  .allrow button { flex:1; }
  .estop { width:100%; background:#30363d; }

  .stat { color:var(--muted); font-size:12px; margin-top:4px; min-height:16px; text-align:center; }
</style>
</head>
<body>
  <h1>SpiderCam &mdash; Perimeter Inspection</h1>

  <!-- Section 1 — status bar (sticky) -->
  <div class="card statusbar">
    <div class="statline">
      <span class="poslbl">Position</span>
      <span>X: <b id="sx">--</b></span>
      <span>Y: <b id="sy">--</b></span>
      <span>Z: <b id="sz">--</b></span>
      <button class="homebtn" id="btnHome" data-lock>&#127968; HOME</button>
    </div>
    <div class="statline cables">
      <span class="poslbl">Cables</span>
      <span>M1: <b id="cm1">--</b>cm</span>
      <span>M2: <b id="cm2">--</b>cm</span>
      <span>M3: <b id="cm3">--</b>cm</span>
      <span>M4: <b id="cm4">--</b>cm</span>
    </div>
  </div>

  <!-- Section 2 — go to position -->
  <div class="card">
    <div class="sechead">Go To Position</div>
    <div class="xyzrow">
      <label>X (cm)<input class="xyzin" id="gx" data-lock type="text" inputmode="decimal" autocomplete="off"></label>
      <label>Y (cm)<input class="xyzin" id="gy" data-lock type="text" inputmode="decimal" autocomplete="off"></label>
      <label>Z (cm)<input class="xyzin" id="gz" data-lock type="text" inputmode="decimal" autocomplete="off"></label>
      <button class="gotobtn" id="btnGoto" data-lock>GO</button>
    </div>
    <div class="calc">Cables &rarr;
      <span>M1: <b id="gl1">--</b></span><span>M2: <b id="gl2">--</b></span>
      <span>M3: <b id="gl3">--</b></span><span>M4: <b id="gl4">--</b></span>
    </div>
    <div class="gwarn" id="gwarn"></div>
    <div class="prerow">
      <button class="preset" id="preCenter" data-lock>Center</button>
      <button class="preset" id="preInspect" data-lock>Inspection Height</button>
    </div>
  </div>

  <!-- Section 3 — perimeter corners -->
  <div class="card">
    <div class="sechead">Perimeter Corners</div>
    <div class="cornergrid" id="cornergrid"></div>
  </div>

  <!-- Section 4 — perimeter route -->
  <div class="card">
    <div class="sechead">Perimeter Route</div>
    <div class="routeline">Route: Raise (Z30) &rarr; Corner 1 &rarr; Corner 2 &rarr; Corner 3 &rarr; Corner 4 &rarr; above Home (Z30) &rarr; Home</div>
    <button class="runbtn" id="btnRun" disabled>RUN PERIMETER ROUTE</button>
    <div class="missing" id="missing"></div>
    <div class="progwrap" id="progwrap" style="display:none;">
      <div class="progbar"><div class="progfill" id="progfill"></div></div>
      <div class="progrow">
        <span class="proglabel" id="proglabel">&mdash;</span>
        <button class="stopplay" id="btnStop">&#9632; STOP</button>
      </div>
    </div>
  </div>

  <!-- Section 5 — manual jog (collapsed) -->
  <details class="card jogd">
    <summary>Manual Jog Controls</summary>
    <div class="jinner">
      <div class="zhead"><span>Z-Lock Movement</span><span id="zval">Z: --.- cm</span></div>
      <div class="zstep">
        <span>Step:</span>
        <label><input type="radio" name="zstep" value="50">Fine (50)</label>
        <label><input type="radio" name="zstep" value="200" checked>Medium (200)</label>
        <label><input type="radio" name="zstep" value="500">Coarse (500)</label>
      </div>
      <div class="zpad">
        <button class="zy" id="zYp" data-z>Y+</button>
        <button id="zXm" data-z>X&minus;</button>
        <button id="zXp" data-z>X+</button>
        <button class="zy" id="zYm" data-z>Y&minus;</button>
      </div>
      <div class="grid" id="jogmotors"></div>
      <div class="allrow">
        <button class="wind"   data-m="all" data-dir="wind">WIND ALL</button>
        <button class="unwind" data-m="all" data-dir="unwind">UNWIND ALL</button>
      </div>
      <button class="estop" id="btnStopAll">STOP ALL MOTORS</button>
    </div>
  </details>

  <div class="stat" id="stat"></div>

<script>
// ── IK (mirrors firmware) + advisory limits ──────────────────────────────────
const PULLEY = [[0,0,93],[135,0,93],[135,135,93],[0,135,93]];
const OFFSET = [[-10.5,-10.5],[10.5,-10.5],[10.5,10.5],[-10.5,10.5]];
const LIM = { x:[15,120], y:[15,120], z:[0,80] };   // ADVISORY only: highlight red, never block
const PERI_LABELS = ['Raising from home','Moving to Corner 1','Moving to Corner 2',
                     'Moving to Corner 3','Moving to Corner 4','Returning above home','Going home'];

const stat = document.getElementById('stat');
function setStat(t){ stat.textContent = t; }
function fmt(v){ return (v == null || isNaN(v)) ? '?' : Number(v).toFixed(1); }

// fetch JSON from an endpoint; throw on HTTP error or {"error":...}
async function jget(url){
  const r = await fetch(url);
  const j = await r.json().catch(() => ({}));
  if (!r.ok || (j && j.error)) throw new Error((j && j.error) || ('HTTP ' + r.status));
  return j;
}

function ikJS(x,y,z){
  const L = [];
  for (let i = 0; i < 4; i++){
    const dx = PULLEY[i][0] - (x + OFFSET[i][0]);
    const dy = PULLEY[i][1] - (y + OFFSET[i][1]);
    const dz = PULLEY[i][2] - z;
    L.push(Math.sqrt(dx*dx + dy*dy + dz*dz));
  }
  return L;
}

// ── Section 2: Go To Position ─────────────────────────────────────────────────
const gx = document.getElementById('gx'), gy = document.getElementById('gy'), gz = document.getElementById('gz');
const gwarn = document.getElementById('gwarn');
function readXYZ(){ return [parseFloat(gx.value), parseFloat(gy.value), parseFloat(gz.value)]; }

// Live cable preview + advisory out-of-range highlight. Never blocks GO.
function previewIK(){
  const [x,y,z] = readXYZ();
  const have = [x,y,z].every(v => !isNaN(v));
  const L = have ? ikJS(x,y,z) : [];
  for (let i = 0; i < 4; i++){
    document.getElementById('gl'+(i+1)).textContent = have ? L[i].toFixed(1) : '--';
  }
  const flag = (el,v,lim) => el.classList.toggle('bad', !isNaN(v) && (v < lim[0] || v > lim[1]));
  flag(gx,x,LIM.x); flag(gy,y,LIM.y); flag(gz,z,LIM.z);
  let warn = '';
  if (have){
    if      (x < LIM.x[0] || x > LIM.x[1]) warn = 'X outside ' + LIM.x[0] + '–' + LIM.x[1] + ' cm (advisory)';
    else if (y < LIM.y[0] || y > LIM.y[1]) warn = 'Y outside ' + LIM.y[0] + '–' + LIM.y[1] + ' cm (advisory)';
    else if (z < LIM.z[0] || z > LIM.z[1]) warn = 'Z outside ' + LIM.z[0] + '–' + LIM.z[1] + ' cm (advisory)';
  }
  gwarn.textContent = warn ? ('⚠ ' + warn) : '';
}
[gx,gy,gz].forEach(el => el.addEventListener('input', previewIK));

async function sendGoto(){
  if (locked) return;
  const [x,y,z] = readXYZ();
  if ([x,y,z].some(v => isNaN(v))){ setStat('Enter X, Y and Z'); return; }
  setStat('Moving to (' + x.toFixed(1) + ', ' + y.toFixed(1) + ', ' + z.toFixed(1) + ')…');
  try { await jget('/goto?x=' + x.toFixed(2) + '&y=' + y.toFixed(2) + '&z=' + z.toFixed(2)); }
  catch(e){ const m = String(e.message); setStat(m.indexOf('busy') >= 0 ? 'busy — a move is already running' : 'GOTO rejected: ' + m); }
}
document.getElementById('btnGoto').addEventListener('click', sendGoto);
gz.addEventListener('keydown', e => { if (e.key === 'Enter'){ e.preventDefault(); sendGoto(); } });

function setPreset(x,y,z){ gx.value = x.toFixed(1); gy.value = y.toFixed(1); gz.value = z.toFixed(1); previewIK(); }
document.getElementById('preCenter').addEventListener('click',  () => setPreset(67.5, 67.5, 53.0));
document.getElementById('preInspect').addEventListener('click', () => setPreset(67.5, 67.5, 66.0));

// ── Section 1: HOME (centre, ground) ──────────────────────────────────────────
async function sendHome(){
  if (locked) return;
  if (!confirm('Move to home position (center, Z = 0)?')) return;
  setStat('Moving to home…');
  try { await jget('/goto?x=67.50&y=67.50&z=0.00'); }
  catch(e){ const m = String(e.message); setStat(m.indexOf('busy') >= 0 ? 'busy — a move is already running' : 'HOME rejected: ' + m); }
}
document.getElementById('btnHome').addEventListener('click', sendHome);

// ── Section 3: Perimeter corners ──────────────────────────────────────────────
let cornersSet = [false,false,false,false];
const cornerGrid = document.getElementById('cornergrid');
for (let i = 0; i < 4; i++){
  const c = document.createElement('div');
  c.className = 'cornercard'; c.id = 'corner' + i;
  c.innerHTML =
    '<div class="cname">Corner ' + (i+1) + '</div>' +
    '<div class="cxyz" id="cxyz' + i + '">Not set</div>' +
    '<div class="crow">' +
      '<button class="csave" data-csave="' + i + '">Save here</button>' +
      '<button class="cgo" data-cgo="' + i + '" disabled>Go</button>' +
    '</div>' +
    '<button class="cclear" data-cclear="' + i + '" disabled>Clear</button>';
  cornerGrid.appendChild(c);
}

function renderCorners(data){
  const cs = (data && data.corners) || [];
  for (let i = 0; i < 4; i++){
    const c = cs[i] || { set:false };
    cornersSet[i] = !!c.set;
    document.getElementById('corner' + i).classList.toggle('set', !!c.set);
    document.getElementById('cxyz' + i).innerHTML = c.set
      ? ('X: ' + fmt(c.x) + '<br>Y: ' + fmt(c.y) + '<br>Z: ' + fmt(c.z))
      : 'Not set';
  }
  refreshControls();
}
async function loadCorners(){ try { renderCorners(await jget('/corner/list')); } catch(e){ /* retry next action */ } }

// Enable/disable corner Go/Clear/Save + RUN based on set-state and lock-state.
function refreshControls(){
  for (let i = 0; i < 4; i++){
    document.querySelector('[data-csave="'  + i + '"]').disabled = locked;
    document.querySelector('[data-cgo="'    + i + '"]').disabled = locked || !cornersSet[i];
    document.querySelector('[data-cclear="' + i + '"]').disabled = locked || !cornersSet[i];
  }
  const missing = [];
  for (let i = 0; i < 4; i++) if (!cornersSet[i]) missing.push('Corner ' + (i+1));
  const allSet = missing.length === 0;
  document.getElementById('btnRun').disabled = locked || !allSet;
  document.getElementById('missing').textContent = allSet ? '' : ('Missing: ' + missing.join(', '));
}

cornerGrid.addEventListener('click', async (e) => {
  const t = e.target;
  if (locked) return;
  if (t.dataset.csave !== undefined){
    const i = t.dataset.csave;
    try { renderCorners(await jget('/corner/save?index=' + i)); setStat('Corner ' + (+i+1) + ' saved from current position'); }
    catch(err){ setStat('save corner failed'); }
  } else if (t.dataset.cgo !== undefined){
    const i = t.dataset.cgo;
    try { await jget('/corner/goto?index=' + i); setStat('Moving to Corner ' + (+i+1) + '…'); }
    catch(err){ const m = String(err.message); setStat(m.indexOf('busy') >= 0 ? 'busy — a move is already running' : 'go to corner failed'); }
  } else if (t.dataset.cclear !== undefined){
    const i = t.dataset.cclear;
    try { renderCorners(await jget('/corner/clear?index=' + i)); setStat('Corner ' + (+i+1) + ' cleared'); }
    catch(err){ setStat('clear corner failed'); }
  }
});

// ── Section 4: Perimeter route ────────────────────────────────────────────────
document.getElementById('btnRun').addEventListener('click', async () => {
  const btn = document.getElementById('btnRun');
  if (locked || btn.disabled) return;
  if (!confirm('Run perimeter route?\nGantry will visit all 4 corners, return to Corner 1, then go Home.')) return;
  try { await jget('/perimeter/play'); setStat('Perimeter route started'); }
  catch(e){
    const m = String(e.message);
    if (m.indexOf('corners_not_set') >= 0) setStat('Set all 4 corners first');
    else if (m.indexOf('busy') >= 0)       setStat('busy — a move is already running');
    else setStat('route start failed');
  }
});
document.getElementById('btnStop').addEventListener('click', async () => {
  try { await fetch('/stopplay'); setStat('Route stopped'); }
  catch(e){ setStat('stop send failed'); }
});

// ── Live state poll (no WebSocket — synchronous WebServer) ────────────────────
let locked = false;
let prevMove = false, prevPeri = false;

function setLocked(isLocked){
  locked = isLocked;
  document.querySelectorAll('[data-lock]').forEach(b => b.disabled = isLocked);    // HOME, GO, presets, XYZ inputs
  document.querySelectorAll('button[data-m]').forEach(b => b.disabled = isLocked); // jog
  document.querySelectorAll('button[data-z]').forEach(b => b.disabled = isLocked); // z-lock
  document.getElementById('btnStopAll').disabled = isLocked;
  refreshControls();                                                               // corner buttons + RUN
}

function applyState(j){
  if (!j || !j.m) return;          // ignore non-state replies (e.g. a 409 {"error":"busy"})
  for (let i = 0; i < 4; i++){
    const el = document.getElementById('cm' + (i+1));
    if (el) el.textContent = j.m[i].toFixed(1);
  }
  if (j.xyz){
    document.getElementById('sx').textContent = j.xyz[0].toFixed(1);
    document.getElementById('sy').textContent = j.xyz[1].toFixed(1);
    document.getElementById('sz').textContent = j.xyz[2].toFixed(1);
    const zv = document.getElementById('zval'); if (zv) zv.textContent = 'Z: ' + j.xyz[2].toFixed(1) + ' cm';
  }

  const move = j.move || { active:false };
  const peri = j.peri || { active:false, index:0, total:6 };
  const reset = j.reset || { active:false };
  const play = j.play || { active:false };

  // Perimeter progress bar + step label (live during the route only).
  const pw = document.getElementById('progwrap');
  if (peri.active){
    pw.style.display = 'block';
    const total = peri.total || 6;
    const pct = Math.min(100, Math.round((peri.index + 1) / total * 100));
    document.getElementById('progfill').style.width = pct + '%';
    const label = PERI_LABELS[peri.index] || ('Step ' + (peri.index + 1));
    document.getElementById('proglabel').textContent = 'Step ' + (peri.index + 1) + ' of ' + total + ' — ' + label;
    setStat('Perimeter: ' + label);
  } else {
    pw.style.display = 'none';
    if (prevPeri){ setStat('Perimeter route complete — home'); loadCorners(); }
  }

  // Single-move (GOTO / corner Go / Z-lock) completion feedback.
  if (!move.active && prevMove && !peri.active) setStat('Move complete');

  prevMove = move.active; prevPeri = peri.active;
  setLocked(move.active || peri.active || reset.active || play.active);
}
async function pollState(){
  try { const r = await fetch('/positions'); applyState(await r.json()); }
  catch(e){ /* transient; next tick retries */ }
}

// ── Section 5: Manual jog + Z-lock ────────────────────────────────────────────
const jogEl = document.getElementById('jogmotors');
for (let m = 1; m <= 4; m++){
  const c = document.createElement('div');
  c.innerHTML =
    '<span class="mname">Motor ' + m + '</span>' +
    '<div class="jrow">' +
      '<button class="wind"   data-m="' + m + '" data-dir="wind">WIND</button>' +
      '<button class="unwind" data-m="' + m + '" data-dir="unwind">UNWIND</button>' +
    '</div>';
  jogEl.appendChild(c);
}

async function sendJog(m, dir){
  try { const r = await fetch('/jog?m=' + m + '&dir=' + dir); applyState(await r.json()); }
  catch(e){ /* keepalive retries */ }
}
async function sendJogStop(m){
  try { const r = await fetch('/jogstop?m=' + m); applyState(await r.json()); }
  catch(e){ setStat('stop send failed — deadman will halt motor'); }
}
function bindHold(btn){
  const m = btn.dataset.m, dir = btn.dataset.dir;
  let ka = null;
  const start = (ev) => {
    ev.preventDefault();
    if (ka || locked) return;
    btn.classList.add('holding');
    sendJog(m, dir);
    ka = setInterval(() => sendJog(m, dir), 150);
  };
  const end = () => {
    if (!ka) return;
    clearInterval(ka); ka = null;
    btn.classList.remove('holding');
    sendJogStop(m);
  };
  btn.addEventListener('pointerdown',   start);
  btn.addEventListener('pointerup',     end);
  btn.addEventListener('pointerleave',  end);
  btn.addEventListener('pointercancel', end);
}
document.querySelectorAll('button[data-m]').forEach(bindHold);

document.getElementById('btnStopAll').addEventListener('click', () => {
  document.querySelectorAll('button.holding').forEach(b => b.classList.remove('holding'));
  sendJogStop('all'); setStat('all motors stopped');
});

function zStep(){ const r = document.querySelector('input[name=zstep]:checked'); return r ? parseInt(r.value, 10) : 200; }
async function sendZlock(d){
  if (locked) return;
  const n = zStep();
  try { await jget('/zlock?d=' + d + '&steps=' + n); setStat('Z-lock ' + d.toUpperCase() + ' — ' + n + ' steps…'); }
  catch(e){ setStat(String(e.message).indexOf('busy') >= 0 ? 'busy — wait for current move' : 'z-lock failed'); }
}
[['zXp','xp'],['zXm','xm'],['zYp','yp'],['zYm','ym']].forEach(([id,d]) =>
  document.getElementById(id).addEventListener('click', () => sendZlock(d)));

// ── Init ──────────────────────────────────────────────────────────────────────
loadCorners();
previewIK();
setInterval(pollState, 150);
pollState();
</script>
</body>
</html>)HTMLPAGE";
