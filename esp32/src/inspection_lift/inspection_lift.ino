// =============================================================================
// SpiderCam — inspection_lift
//
// Raises and lowers the central unit on command from the Argus Pi software.
// This is the production integration of two proven test sketches:
//
//   • command_test                     — the Pi → ESP32 HTTP command link
//       (WiFi STA + static IP + mDNS, the Task-WDT fix, /start_inspection …).
//   • collection_test/v2_1_manual_jog  — the 4× DRV8825 STEP/DIR stepper driver
//       (Bresenham-coordinated stepping, STEPS_PER_CM calibration).
//
// The unit hangs on 4 cables. Winding all four in equally reels it straight up;
// paying all four out equally lowers it. "Wire extended" is the length of cable
// each motor has paid out — identical on all four for a vertical move.
//
//   POST /start_inspection  → RAISE : 118 cm → 85 cm  (wind in 33 cm)
//   POST /stop  |  /abort    → LOWER :  85 cm → 118 cm (pay out 33 cm)
//
// /stop is the manual-control halt (esp_client.stop); /abort is what the Argus
// inspection "Stop" button actually sends (events.py _on_stop_scan → esp.abort()).
// Both return the unit to its resting position, so either one halts a pass and
// stows the unit. The remaining inspection commands (pause/resume/estop/release/
// move/home) are acknowledged no-ops so the Pi's polling link never sees a 404.
//
// The move runs on a dedicated motor task pinned to Core 0, so the HTTP server
// stays responsive (the Pi keeps polling /ping ~0.5 Hz). A command that arrives
// while a move is in flight is rejected with 409 busy rather than corrupting it,
// and a repeated command that finds the unit already in position is a no-op.
//
// WiFi credentials live in secrets.h (gitignored). Serial Monitor @ 115200 shows
// the assigned IP and every [CMD]/[LIFT] event.
// =============================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <math.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_task_wdt.h"
#include "secrets.h"            // defines WIFI_SSID / WIFI_PASS (gitignored)

WebServer server(80);

// ── Lift geometry ─────────────────────────────────────────────────────────────
// Wire paid out by each cable at the two positions (cm). RAISED is shorter wire.
const float LOWERED_WIRE_CM = 118.0f;    // resting / stowed position
const float RAISED_WIRE_CM  =  90.0f;    // inspection height

// ── Stepper config (from collection_test/v2_1_manual_jog) ─────────────────────
const int STEP_PINS[4] = { 23, 17, 18, 21 };
const int DIR_PINS[4]  = { 22, 16,  5, 19 };

// Per-motor direction invert. Set true for any motor wound opposite the
// software convention (positive steps must reel cable IN on every motor).
const bool DIR_INVERT[4] = { false, false, false, false };

const int          STEPS_PER_REV = 200;     // 1.8° full steps per revolution
const float        STEPS_PER_CM  = 15.92f;  // calibrated: 200 / <cm per rotation>

// ── Move speed (per-step pulse delay, µs) ─────────────────────────────────────
// Speed is set by the delay between step pulses: each step is two delays
// (HIGH + LOW), so step rate ≈ 1 / (2 × delay) — speed is INVERSELY proportional
// to the delay. A LARGER delay = SLOWER motion. Each direction has its own
// constant so raise and lower can be tuned independently; tweak these to retune.
//
// Reduced to 1/3 of the previous speed: the prior delay was 2400 µs, and 1/3
// speed means 3× the delay → 7200 µs. (That earlier 2400 µs was itself 1/3 of
// v2_1's original 800 µs GO_DELAY_US, so 7200 µs is now 1/9 of full speed.)
const unsigned int RAISE_STEP_DELAY_US = 10800;  // 118 → 85 cm  (1/3 of the prior 2400 µs speed)
const unsigned int LOWER_STEP_DELAY_US = 10800;  // 85 → 118 cm  (1/3 of the prior 2400 µs speed)

// ── Reachability: DHCP + mDNS ─────────────────────────────────────────────────
// This hotspot's DHCP subnet is NOT fixed: command_test pinned 192.168.85.85
// from a prior lease, but the phone now hands out 192.168.253.0/24 — so a
// hardcoded static IP goes stale and strands the board on a phantom subnet,
// unreachable from the Pi even though both are on the same AP. Use DHCP and let
// the hotspot assign an address on its current subnet; the Pi resolves the board
// by name via mDNS ("spidercam.local", the Pi's default ESP32_IP in config.py).
const char *MDNS_HOST = "spidercam";        // -> spidercam.local

// ── Lift state ────────────────────────────────────────────────────────────────
// Tracked wire length (cm) currently paid out, common to all 4 cables. Boot
// assumes the unit is resting/stowed at LOWERED_WIRE_CM — match this physically
// before powering on (same assumption v2_1 makes about its start position).
volatile float currentWireCm   = LOWERED_WIRE_CM;
volatile float pendingTargetCm = LOWERED_WIRE_CM;
volatile bool  isMoving        = false;     // a move is in flight on motorTask
TaskHandle_t   motorTaskHandle = NULL;

// =============================================================================
// Motor moves — Bresenham-coordinated 4-motor stepping (from v2_1_manual_jog).
// All four cables step together so the unit travels straight up / down.
// =============================================================================
void runAllBresenham(const long steps[4], unsigned int delayUs) {
    long absS[4], maxS = 0;
    for (int i = 0; i < 4; i++) {
        absS[i] = labs(steps[i]);
        if (absS[i] > maxS) maxS = absS[i];
    }
    if (maxS == 0) return;

    for (int i = 0; i < 4; i++) {
        bool fwd = (steps[i] >= 0) ^ DIR_INVERT[i];
        digitalWrite(DIR_PINS[i], fwd ? HIGH : LOW);
    }
    delayMicroseconds(20);

    long acc[4] = {0, 0, 0, 0};
    for (long t = 0; t < maxS; t++) {
        bool fire[4] = {false, false, false, false};
        for (int i = 0; i < 4; i++) {
            if (absS[i] == 0) continue;
            acc[i] += absS[i];
            if (acc[i] >= maxS) { acc[i] -= maxS; fire[i] = true; }
        }
        for (int i = 0; i < 4; i++) if (fire[i]) digitalWrite(STEP_PINS[i], HIGH);
        delayMicroseconds(delayUs);
        for (int i = 0; i < 4; i++) if (fire[i]) digitalWrite(STEP_PINS[i], LOW);
        delayMicroseconds(delayUs);
        if (t % 100 == 0) yield();          // let Core 0's WiFi task breathe
    }
}

// Move all four cables to `targetCm` of wire extended, then update the tracked
// length. Positive steps wind / reel in (shorter wire = raise); negative steps
// unwind / pay out (longer wire = lower) — same sign convention as v2_1.
void moveWireTo(float targetCm) {
    float fromCm  = currentWireCm;
    float deltaCm = fromCm - targetCm;                  // >0 ⇒ wind in (raise)
    long  s       = lroundf(deltaCm * STEPS_PER_CM);
    long  steps[4] = { s, s, s, s };

    // Raising winds cable in (deltaCm ≥ 0); lowering pays it out (deltaCm < 0).
    // Pick the speed for that direction.
    unsigned int delayUs = (deltaCm >= 0) ? RAISE_STEP_DELAY_US : LOWER_STEP_DELAY_US;

    Serial.printf("[LIFT] %.1f → %.1f cm  (%s %.1f cm, %ld steps/motor, %u us/step)  t=%lums\n",
        fromCm, targetCm, s >= 0 ? "wind" : "unwind",
        fabsf(deltaCm), labs(s), delayUs, millis());

    runAllBresenham(steps, delayUs);
    currentWireCm = targetCm;

    Serial.printf("[LIFT] done — %.1f cm extended  t=%lums\n", currentWireCm, millis());
}

// Motor task body: run the queued move, then clear the busy flag and self-delete.
void motorTask(void *param) {
    moveWireTo(pendingTargetCm);
    isMoving = false;
    motorTaskHandle = NULL;
    vTaskDelete(NULL);
}

// =============================================================================
// Command handlers
// =============================================================================
// Kick off a move to `targetCm` on the motor task, replying:
//   200 {"status":"ok"}    — move started, or already in position (no-op)
//   409 {"status":"busy"}  — a move is already in flight (command ignored)
// `label` is logged so the serial trace shows which command triggered it.
static void startMove(float targetCm, const char *label) {
    if (isMoving) {
        Serial.printf("[CMD] %s — ignored, move in progress\n", label);
        server.send(409, "application/json", "{\"status\":\"busy\"}");
        return;
    }
    if (fabsf(currentWireCm - targetCm) < 0.05f) {
        Serial.printf("[CMD] %s — already in position (%.1f cm), holding\n",
            label, currentWireCm);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
        return;
    }
    Serial.printf("[CMD] %s — moving to %.1f cm\n", label, targetCm);
    pendingTargetCm = targetCm;
    isMoving = true;
    xTaskCreatePinnedToCore(motorTask, "lift", 4096, NULL, 1, &motorTaskHandle, 0);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleStartInspection() { startMove(RAISED_WIRE_CM,  "start_inspection (raise)"); }
static void handleStop()            { startMove(LOWERED_WIRE_CM, "stop (lower)"); }
static void handleAbort()           { startMove(LOWERED_WIRE_CM, "abort (lower)"); }

// /ping is intentionally SILENT — the Pi polls it ~0.5 Hz for reachability;
// printing it would bury the real commands.
static void handlePing() { server.send(200, "application/json", "{\"status\":\"ok\"}"); }

// Inspection commands this 1-DOF lift does not act on, acknowledged so the Pi's
// link never 404s. Moves are short and run to completion, so an estop/abort that
// arrives mid-move is honoured by the next /stop|/abort once the move releases.
static void ackNoop(const char *label) {
    Serial.printf("[CMD] %s — acknowledged (no-op)\n", label);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}
static void handlePause()   { ackNoop("pause"); }
static void handleResume()  { ackNoop("resume"); }
static void handleEstop()   { ackNoop("ESTOP"); }
static void handleRelease() { ackNoop("release"); }
static void handleMove()    { ackNoop("move"); }
static void handleHome()    { ackNoop("home"); }

// /position — kept x/y zero (this lift has no XY); omit z so the Pi's poller
// leaves its simulated head position untouched (events._esp_loop only applies a
// position when x, y AND z are present).
static void handlePosition() {
    server.send(200, "application/json", "{\"x\":0,\"y\":0}");
}

// /status — report the lift state and tracked wire length for debugging.
static void handleStatus() {
    const char *state = isMoving ? "moving"
                      : (fabsf(currentWireCm - RAISED_WIRE_CM) < 0.05f) ? "raised"
                      : "resting";
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"state\":\"%s\",\"wire_cm\":%.1f}",
             state, currentWireCm);
    server.send(200, "application/json", buf);
}

static void handleNotFound() {
    Serial.printf("[HTTP] 404 %s\n", server.uri().c_str());
    server.send(404, "application/json", "{\"status\":\"not_found\"}");
}

// ── WiFi (verbatim from command_test) ─────────────────────────────────────────
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
        Serial.printf("[mDNS] reachable at http://%s.local\n", MDNS_HOST);
    } else {
        Serial.println("[mDNS] start failed (use the static IP instead)");
    }
}

// ── setup / loop ──────────────────────────────────────────────────────────────
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // disable brownout detector

    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[boot] inspection_lift starting...");

    // Stepper pins idle (from v2_1). The unit is assumed resting at LOWERED_WIRE_CM.
    for (int i = 0; i < 4; i++) {
        pinMode(STEP_PINS[i], OUTPUT);
        pinMode(DIR_PINS[i],  OUTPUT);
        digitalWrite(STEP_PINS[i], LOW);
        digitalWrite(DIR_PINS[i],  LOW);
    }
    currentWireCm = LOWERED_WIRE_CM;
    Serial.printf("[boot] assuming resting position — %.1f cm wire extended\n",
                  currentWireCm);

    // The WiFi driver starves Core 0's idle task; the Task WDT watches both
    // cores' idle tasks by default and would fire TG0WDT_SYS_RESET a few seconds
    // after the radio comes up. Reconfigure the WDT to watch no idle task
    // (idle_core_mask = 0) — avoids the reset without leaving a dangling idle
    // hook spamming "task_wdt ... task not found". (see memory: esp32-wifi-wdt-fix)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,
        .trigger_panic = false,
    };
    esp_task_wdt_reconfigure(&wdt_config);

    connectWiFi();

    server.enableCORS(true);
    server.on("/ping",             HTTP_GET,  handlePing);
    server.on("/start_inspection", HTTP_POST, handleStartInspection);  // → RAISE
    server.on("/stop",             HTTP_POST, handleStop);             // → LOWER
    server.on("/abort",            HTTP_POST, handleAbort);            // → LOWER
    server.on("/pause",            HTTP_POST, handlePause);
    server.on("/resume",           HTTP_POST, handleResume);
    server.on("/estop",            HTTP_POST, handleEstop);
    server.on("/release",          HTTP_POST, handleRelease);
    server.on("/move",             HTTP_POST, handleMove);
    server.on("/home",             HTTP_POST, handleHome);
    server.on("/position",         HTTP_GET,  handlePosition);
    server.on("/status",           HTTP_GET,  handleStatus);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.printf("[HTTP] Server ready at http://%s\n", WiFi.localIP().toString().c_str());
    Serial.println("[HTTP] start_inspection → raise to 85 cm,  stop/abort → lower to 118 cm");
}

void loop() {
    server.handleClient();

    // If WiFi drops, reconnect so the command link recovers on its own.
    static bool wasConnected = true;
    if (WiFi.status() != WL_CONNECTED) {
        if (wasConnected) { Serial.println("[WiFi] Lost connection — reconnecting..."); wasConnected = false; }
        connectWiFi();
        wasConnected = true;
    }
}
