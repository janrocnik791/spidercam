// =============================================================================
// Argus Autonomous — v1_foundation.ino
// V1: WiFi AP · Hardware & Software E-Stop · Z-Level Homing · Go to Start
// =============================================================================
//
// Hardware: ESP32 + 4× DRV8825 stepper drivers
// Working volume: 200 × 200 × 94 cm
//
// Pulley corner positions (Z = 0 at pulley level, negative downward):
//   M1 = (  0,   0, 0)    M2 = (200,   0, 0)
//   M3 = (200, 200, 0)    M4 = (  0, 200, 0)
//
// Pin map (authoritative — verified on physical hardware via test_all_motors):
//   Motor 1  STEP=23  DIR=22      Motor 2  STEP=17  DIR=16
//   Motor 3  STEP=18  DIR=5       Motor 4  STEP=21  DIR=19
//   Shared /ENABLE (all 4 DRV8825):  GPIO 26  (active LOW)
//   Z-level switch (NO contact):     GPIO 33  (active HIGH when pressed)
//   Hardware E-stop detect (NC):     GPIO 27  (active HIGH when E-stop fires)
//
// WiFi: Access Point — SSID "SpiderCam", open network, IP 192.168.4.1
//       Open a browser to http://192.168.4.1 to control the unit.
//
// State machine:
//   UNINIT   →  (Z-switch pressed)     →  HOMED
//   HOMED    →  (/go_to_start POST)    →  MOVING
//   MOVING   →  (move complete)        →  AT_START
//   any      →  (E-stop triggered)     →  ESTOPPED
//   ESTOPPED →  (/reset POST)          →  UNINIT  (must re-home)
//
// Serial output (115200 baud):
//   All state changes and step counts are logged.
//   Connect the Serial Monitor to watch the system boot and operate.
//
// ✓ V1 test: trigger Z-switch → press Go to Start → unit moves to M1 corner.
//             Both E-stops (hardware NC switch and UI button) cut motion
//             immediately at any point.
// =============================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_task_wdt.h"

// ─── Pin assignments ──────────────────────────────────────────────────────────

// Verified against test_all_motors.ino — physical ground truth.
// DIR=HIGH → reel in (cable shortens, unit rises / moves toward that corner).
// DIR=LOW  → pay out (cable lengthens, unit descends / moves away).
const int STEP_PINS[4] = { 23, 17, 18, 21 };
const int DIR_PINS[4]  = { 22, 16,  5, 19 };

// Shared /ENABLE line for all four DRV8825 drivers.
// Set to -1 if your wiring does not bring ENABLE to an ESP32 GPIO.
// When wired: LOW = motors enabled (normal), HIGH = motors disabled (E-stop).
const int ENABLE_PIN   = 26;

// Z-level homing switch — Normally Open (NO), active HIGH when pressed.
const int Z_SWITCH_PIN = 33;

// Hardware E-stop detection — Normally Closed (NC) switch connected so that
// opening the switch (pressing E-stop) drives this pin HIGH via the physical
// ENABLE-line interrupt; also read in loop() as a belt-and-suspenders check.
// Convention matches the existing sketch: INPUT_PULLDOWN, HIGH = E-stop active.
const int ESTOP_PIN    = 27;

// ─── Machine geometry (centimetres) ──────────────────────────────────────────

const float FRAME_W         = 200.0f;  // X span, M1→M2 / M4→M3
const float FRAME_D         = 200.0f;  // Y span, M1→M4 / M2→M3
// Z convention: 0 = pulley level (top of frame), negative = downward.
// Full working range: 0 (top) to −FRAME_H (floor level).
const float FRAME_H         = 94.0f;   // vertical span, pulleys to floor (positive)
const float SPOOL_RADIUS_CM = 2.0f;    // spool radius (20 mm)
const float LINE_OFFSET_CM  = 8.0f;    // fixed cable offset subtracted per motor

// ─── Motor / stepping ────────────────────────────────────────────────────────

const int          MOTOR_STEPS_PER_REV = 200;
const int          MICROSTEPS          = 1;
const unsigned int STEP_DELAY_US       = 800;   // µs per half-step period
const unsigned int DIR_SETUP_US        = 20;    // DIR → STEP setup time (µs)

// Derived: steps per centimetre of cable travel
const float STEPS_PER_CM =
    (float)(MOTOR_STEPS_PER_REV * MICROSTEPS) / (2.0f * (float)M_PI * SPOOL_RADIUS_CM);

// ─── WiFi ────────────────────────────────────────────────────────────────────

const char* WIFI_SSID = "SpiderCam";
// Open network — no password.  Pass nullptr to softAP() for open mode.

WebServer server(80);

// ─── State machine ───────────────────────────────────────────────────────────

enum SysState { UNINIT, HOMED, MOVING, AT_START, ESTOPPED };
SysState sysState = UNINIT;

const char* STATE_NAMES[] = {
    "UNINIT", "HOMED", "MOVING", "AT_START", "ESTOPPED"
};

// ─── Position ────────────────────────────────────────────────────────────────

struct Pos3D { float x, y, z; };

// Working assumption until homing completes.
// Overwritten to the authoritative homed value when the Z-switch fires.
Pos3D currentPos = { 100.0f, 100.0f, -FRAME_H };

// ─── Non-blocking move state (4-motor Bresenham sync) ────────────────────────

static long  g_steps[4];       // signed step deltas for current move
static long  g_remAbs[4];      // |steps| remaining per motor
static long  g_acc[4];         // Bresenham accumulator per motor
static long  g_masterStep;     // steps executed so far in this move
static long  g_maxSteps;       // total master steps in this move
static Pos3D g_moveTarget;     // destination of current move
static bool  g_moveActive = false;

// ─── Flags written by ISR / HTTP handlers ────────────────────────────────────

volatile bool g_estopReq      = false;  // set by ISR or /estop handler
volatile bool g_goToStartReq  = false;  // set by /go_to_start handler
volatile bool g_resetReq      = false;  // set by /reset handler

// =============================================================================
// Geometry helpers
// =============================================================================

// Pulley corner for motor i (all at Z = 0, the top of the frame):
//   i=0  M1  (    0,       0,     0)
//   i=1  M2  (FRAME_W,     0,     0)
//   i=2  M3  (FRAME_W, FRAME_D,   0)
//   i=3  M4  (    0,   FRAME_D,   0)
//
// Z convention: 0 = pulley plane, negative = downward.
// dz = 0 − p.z = −p.z  (positive when payload hangs below pulleys).
// Because dz is squared, sign does not matter:  (−p.z)² == p.z²
//
// Cable length = straight-line distance from pulley corner to payload position,
// minus the fixed per-motor line offset.
float cableLength(int i, Pos3D p) {
    float mx = (i == 1 || i == 2) ? FRAME_W : 0.0f;
    float my = (i == 2 || i == 3) ? FRAME_D : 0.0f;
    float dx = p.x - mx;
    float dy = p.y - my;
    float dz = -p.z;   // Z=0 at pulley; payload p.z < 0, so -p.z > 0
    return sqrtf(dx * dx + dy * dy + dz * dz) - LINE_OFFSET_CM;
}

// Convert a cable-length delta (cm) to a signed step count.
// The delta passed here is already (currentLength - targetLength), so:
//   positive value → cable must shorten → DIR=HIGH (reel in)  ✓
//   negative value → cable must lengthen → DIR=LOW  (pay out) ✓
long cmToSteps(float dl) {
    return (long)roundf(dl * STEPS_PER_CM);
}

// =============================================================================
// Motor control helpers
// =============================================================================

void enableMotors() {
    if (ENABLE_PIN >= 0) digitalWrite(ENABLE_PIN, LOW);
}

void disableMotors() {
    if (ENABLE_PIN >= 0) digitalWrite(ENABLE_PIN, HIGH);
}

void setDirections(const long steps[4]) {
    for (int i = 0; i < 4; i++)
        digitalWrite(DIR_PINS[i], steps[i] >= 0 ? HIGH : LOW);
    delayMicroseconds(DIR_SETUP_US);
}

// =============================================================================
// Non-blocking move  —  call beginMove() once, then stepMove() each loop()
// =============================================================================

void beginMove(Pos3D target) {
    g_maxSteps = 0;
    for (int i = 0; i < 4; i++) {
        // currentLength - targetLength:
        //   positive → cable shortens → positive steps → DIR=HIGH → reel in ✓
        //   negative → cable lengthens → negative steps → DIR=LOW  → pay out ✓
        float dl    = cableLength(i, currentPos) - cableLength(i, target);
        g_steps[i]  = cmToSteps(dl);
        g_remAbs[i] = labs(g_steps[i]);
        if (g_remAbs[i] > g_maxSteps) g_maxSteps = g_remAbs[i];
    }
    g_masterStep = 0;
    g_acc[0] = g_acc[1] = g_acc[2] = g_acc[3] = 0;
    g_moveTarget = target;
    g_moveActive = true;
    setDirections(g_steps);

    Serial.printf("[MOVE] t=%lums  (%.1f,%.1f,%.1f) → (%.1f,%.1f,%.1f)  steps: %ld %ld %ld %ld  master=%ld\n",
        millis(),
        currentPos.x, currentPos.y, currentPos.z,
        target.x, target.y, target.z,
        g_steps[0], g_steps[1], g_steps[2], g_steps[3], g_maxSteps);
}

// Execute one master step.  Returns true when the move is complete or aborted.
bool stepMove() {
    if (!g_moveActive) return true;

    // Abort immediately if E-stop has been flagged.
    if (g_estopReq) {
        g_moveActive = false;
        return true;
    }

    if (g_masterStep >= g_maxSteps) {
        currentPos   = g_moveTarget;
        g_moveActive = false;
        Serial.printf("[MOVE] Complete t=%lums — arrived at (%.1f, %.1f, %.1f).  Total master steps: %ld\n",
            millis(), currentPos.x, currentPos.y, currentPos.z, g_masterStep);
        return true;
    }

    // Bresenham distribution: decide which motors step this master cycle.
    bool go[4] = { false, false, false, false };
    for (int i = 0; i < 4; i++) {
        if (g_remAbs[i] == 0) continue;
        g_acc[i] += g_remAbs[i];
        if (g_acc[i] >= g_maxSteps) {
            g_acc[i] -= g_maxSteps;
            go[i] = true;
        }
    }

    // Fire STEP pulses simultaneously.
    for (int i = 0; i < 4; i++) if (go[i]) digitalWrite(STEP_PINS[i], HIGH);
    delayMicroseconds(STEP_DELAY_US);
    for (int i = 0; i < 4; i++) if (go[i]) digitalWrite(STEP_PINS[i], LOW);
    delayMicroseconds(STEP_DELAY_US);

    g_masterStep++;

    // Belt-and-suspenders: explicitly reset the watchdog every 100 master steps
    // (~160 ms).  The yield() in loop() is the primary TWDT fix; this catches
    // any edge case where the idle task still doesn't get enough time.
    if (g_masterStep % 100 == 0) {
        esp_task_wdt_reset();
    }

    return false;
}

// =============================================================================
// Hardware E-stop ISR  —  IRAM so it runs even if cache is busy
// =============================================================================

void IRAM_ATTR onEstopISR() {
    g_estopReq = true;   // main loop reads this and disables motors safely
}

// =============================================================================
// HTTP handlers
// =============================================================================

// JSON state snapshot polled by the UI every 500 ms.
// z is sent as fabsf(currentPos.z) — a positive "cm below pulleys" depth value —
// so the UI can display it without sign-conversion logic.
void handleGetState() {
    char buf[128];
    float displayZ = fabsf(currentPos.z);   // e.g. −47.0 → 47.0 cm below top
    snprintf(buf, sizeof(buf),
        "{\"state\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"z\":%.1f}",
        STATE_NAMES[sysState], currentPos.x, currentPos.y, displayZ);
    server.send(200, "application/json", buf);
}

// Software E-stop — mirrors the hardware button in the UI.
void handlePostEstop() {
    g_estopReq = true;
    server.send(200, "text/plain", "E-stop acknowledged");
}

// Reset after E-stop — re-enables motors and returns to UNINIT for re-homing.
void handlePostReset() {
    g_resetReq = true;
    server.send(200, "text/plain", "Reset acknowledged — re-trigger Z-switch to re-home");
}

// Begin movement to the start position (5, 5, current Z).
void handlePostGoToStart() {
    if (sysState != HOMED) {
        server.send(409, "text/plain", "Not homed — trigger Z-switch first");
        return;
    }
    g_goToStartReq = true;
    server.send(200, "text/plain", "Moving to start position (5, 5, z)");
}

// =============================================================================
// Embedded HTML UI  —  stored in flash (PROGMEM) to save RAM
// =============================================================================

const char HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Argus V1</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
       background:#f4f4f4;color:#111}

  /* ── E-stop bar ── always fixed at top, full width, impossible to miss */
  #estop-btn{
    position:fixed;top:0;left:0;width:100%;z-index:999;
    background:#dc2626;color:#fff;border:none;
    font-size:20px;font-weight:800;letter-spacing:1px;
    padding:16px;cursor:pointer;display:block;text-align:center
  }
  #estop-btn:active{background:#991b1b}

  /* ── Main layout ── */
  .wrap{max-width:440px;margin:0 auto;padding:76px 16px 32px}

  /* ── Status card ── */
  .status{background:#fff;border:1.5px solid #e0e0e0;border-radius:10px;
          padding:14px 16px;margin-bottom:14px}
  .status-label{font-size:10px;color:#999;text-transform:uppercase;
                letter-spacing:.8px;margin-bottom:6px}
  #state-text{font-size:22px;font-weight:700;margin-bottom:4px}
  #coords{font-size:13px;color:#666}

  /* ── Cards ── */
  .card{background:#fff;border:1.5px solid #e0e0e0;border-radius:10px;
        padding:16px;margin-bottom:12px}
  .card-title{font-size:10px;color:#999;text-transform:uppercase;
              letter-spacing:.8px;margin-bottom:10px}
  .hint{font-size:12px;color:#777;line-height:1.55;margin-top:8px}

  /* ── Buttons ── */
  .btn{width:100%;padding:13px;font-size:15px;font-weight:600;
       border:none;border-radius:8px;cursor:pointer;transition:opacity .1s}
  .btn:disabled{opacity:.32;cursor:not-allowed}
  .btn-primary{background:#2563eb;color:#fff}
  .btn-primary:not(:disabled):active{background:#1d4ed8}
  .btn-secondary{background:#e5e7eb;color:#374151}
  .btn-secondary:not(:disabled):active{background:#d1d5db}

  /* ── State colour classes ── */
  .s-uninit {color:#9ca3af}
  .s-homed  {color:#16a34a}
  .s-moving {color:#d97706}
  .s-atstart{color:#2563eb}
  .s-estop  {color:#dc2626}
</style>
</head>
<body>

<button id="estop-btn" onclick="doEstop()">&#9940; EMERGENCY STOP</button>

<div class="wrap">

  <!-- Status -->
  <div class="status">
    <div class="status-label">System Status</div>
    <div id="state-text" class="s-uninit">Connecting…</div>
    <div id="coords">X: — &nbsp; Y: — &nbsp; Z: —</div>
  </div>

  <!-- Homing instructions -->
  <div class="card">
    <div class="card-title">Homing</div>
    <p class="hint">
      Manually move the unit near the <strong>M1 corner</strong> (front-left),
      then press the <strong>Z-level switch</strong>. The system records the
      position as (100, 100, −94&nbsp;cm below top) and unlocks movement.
      "Go to Start" will then travel <em>upward</em> and toward M1.
    </p>
  </div>

  <!-- Go to Start -->
  <div class="card">
    <div class="card-title">Navigation</div>
    <button id="btn-go" class="btn btn-primary" onclick="goToStart()" disabled>
      Go to Start &nbsp;(5, 5, Z)
    </button>
    <p class="hint" id="go-hint">Home the unit first.</p>
  </div>

  <!-- Reset -->
  <div class="card">
    <div class="card-title">Recovery</div>
    <button id="btn-rst" class="btn btn-secondary" onclick="doReset()" disabled>
      Reset after E-Stop
    </button>
    <p class="hint">
      After an E-stop: press Reset, then re-trigger the Z-switch to re-home
      before moving again.
    </p>
  </div>

</div><!-- /.wrap -->

<script>
var prevState = '';

function post(url) {
  return fetch(url, {method:'POST'}).then(function(r){return r.text();})
    .catch(function(e){console.warn(url, e);});
}

function doEstop()    { post('/estop'); }
function doReset()    { post('/reset'); }
function goToStart()  {
  document.getElementById('btn-go').disabled = true;
  document.getElementById('go-hint').textContent = 'Moving…';
  post('/go_to_start');
}

var STATE_MAP = {
  UNINIT:   {cls:'s-uninit',  label:'⬜ Waiting for homing'},
  HOMED:    {cls:'s-homed',   label:'✅ Homed — ready to move'},
  MOVING:   {cls:'s-moving',  label:'🔄 Moving to start…'},
  AT_START: {cls:'s-atstart', label:'📍 At start position'},
  ESTOPPED: {cls:'s-estop',   label:'🛑 E-STOP ACTIVE'}
};

function applyState(s, x, y, z) {
  var info = STATE_MAP[s] || {cls:'s-uninit', label:s};

  // Update state label only when state changes (avoids flicker).
  if (s !== prevState) {
    prevState = s;
    var el = document.getElementById('state-text');
    el.className = info.cls;
    el.textContent = info.label;
  }

  // Update coordinate display.
  // z is received as "cm below pulleys" (positive) — label it accordingly.
  document.getElementById('coords').innerHTML =
    'X:&nbsp;' + x.toFixed(1) +
    '&nbsp;&nbsp;Y:&nbsp;' + y.toFixed(1) +
    '&nbsp;&nbsp;Z:&nbsp;' + z.toFixed(1) + '&nbsp;cm below top';

  // Button enable/disable logic.
  document.getElementById('btn-go').disabled  = (s !== 'HOMED');
  document.getElementById('btn-rst').disabled = (s !== 'ESTOPPED');

  document.getElementById('go-hint').textContent =
    s === 'HOMED'    ? 'Ready — press to move to the start corner.' :
    s === 'MOVING'   ? 'Moving…' :
    s === 'AT_START' ? 'Already at start position.' :
    s === 'ESTOPPED' ? 'E-stop active — reset first.' :
                       'Home the unit first.';
}

function poll() {
  fetch('/state')
    .then(function(r){ return r.json(); })
    .then(function(d){ applyState(d.state, d.x, d.y, d.z); })
    .catch(function(e){ /* board temporarily unreachable */ });
}

poll();
setInterval(poll, 500);
</script>
</body>
</html>)rawhtml";

// =============================================================================
// setup()
// =============================================================================

void setup() {
    // Disable the hardware brownout detector — motors can cause brief dips.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Argus V1] Booting…");

    // ── Task Watchdog: extend timeout to 10 s ────────────────────────────────
    // Default TWDT fires in ~3–5 s when the idle task is starved.
    // A full "Go to Start" move takes ~2.4 s, which is dangerously close.
    // 10 s gives comfortable headroom while still catching genuine hangs.
    // IDF 5 / Arduino core 3.x API — takes a config struct instead of two args.
    {
        const esp_task_wdt_config_t wdtConfig = {
            .timeout_ms    = 10000,  // 10-second timeout
            .idle_core_mask = 0,     // don't auto-subscribe idle tasks
            .trigger_panic  = true   // panic-reset on timeout
        };
        esp_task_wdt_init(&wdtConfig);   // reconfigure (or init) the watchdog
        esp_task_wdt_add(NULL);          // subscribe the main Arduino loop task
    }
    Serial.println("[WDT] Task watchdog timeout set to 10 s.");

    // ── Stepper pins ──────────────────────────────────────────────────────────
    for (int i = 0; i < 4; i++) {
        pinMode(STEP_PINS[i], OUTPUT);
        pinMode(DIR_PINS[i],  OUTPUT);
        digitalWrite(STEP_PINS[i], LOW);
        digitalWrite(DIR_PINS[i],  LOW);
    }

    // ── Motor ENABLE pin (active LOW = motors enabled) ────────────────────────
    if (ENABLE_PIN >= 0) {
        pinMode(ENABLE_PIN, OUTPUT);
        enableMotors();
        Serial.printf("[INIT] Motor ENABLE on GPIO %d — motors enabled.\n", ENABLE_PIN);
    } else {
        Serial.println("[INIT] ENABLE_PIN = -1 — E-stop software disable not wired.");
    }

    // ── Z-level switch (NO, active HIGH) ─────────────────────────────────────
    pinMode(Z_SWITCH_PIN, INPUT);
    Serial.printf("[INIT] Z-switch on GPIO %d.\n", Z_SWITCH_PIN);

    // ── Hardware E-stop (NC switch, active HIGH when triggered) ──────────────
    pinMode(ESTOP_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(ESTOP_PIN), onEstopISR, RISING);
    Serial.printf("[INIT] Hardware E-stop on GPIO %d (interrupt on RISING).\n", ESTOP_PIN);

    // Check if E-stop is already active at boot (switch already open).
    if (digitalRead(ESTOP_PIN) == HIGH) {
        g_estopReq = true;
        Serial.println("[INIT] WARNING: E-stop pin is HIGH at boot — starting in ESTOPPED.");
    }

    // ── WiFi AP ───────────────────────────────────────────────────────────────
    WiFi.softAP(WIFI_SSID);
    Serial.printf("[WiFi] AP started — SSID: %s  IP: %s\n",
        WIFI_SSID, WiFi.softAPIP().toString().c_str());

    // ── HTTP routes ───────────────────────────────────────────────────────────
    server.on("/",             HTTP_GET,  []() { server.send_P(200, "text/html", HTML); });
    server.on("/state",        HTTP_GET,  handleGetState);
    server.on("/estop",        HTTP_POST, handlePostEstop);
    server.on("/reset",        HTTP_POST, handlePostReset);
    server.on("/go_to_start",  HTTP_POST, handlePostGoToStart);
    server.begin();
    Serial.println("[HTTP] Server started on port 80.");

    sysState = UNINIT;
    Serial.println("[Argus V1] Ready — trigger the Z-level switch to home.");
}

// =============================================================================
// loop()
// =============================================================================

void loop() {
    // ── Yield to FreeRTOS scheduler ──────────────────────────────────────────
    // CRITICAL: delayMicroseconds() is a busy-spin — it never suspends the task.
    // Without this yield(), the idle task (priority 0) is completely starved
    // while stepMove() is running, and the Task Watchdog fires after ~3–5 s.
    // yield() calls vTaskDelay(0), which gives the idle task one scheduling slot
    // every loop iteration (~1.6 ms during a move) — enough to satisfy the TWDT.
    yield();

    // Keep the HTTP server responsive — handle one pending request per iteration.
    server.handleClient();

    // ── E-stop handling (highest priority) ───────────────────────────────────
    // Also poll the pin directly — belt-and-suspenders in case the ISR missed
    // a very short pulse or the pin was high before the interrupt was attached.
    if (!g_estopReq && digitalRead(ESTOP_PIN) == HIGH) {
        g_estopReq = true;
    }

    if (g_estopReq) {
        g_estopReq  = false;
        g_moveActive = false;
        disableMotors();
        if (sysState != ESTOPPED) {
            sysState = ESTOPPED;
            Serial.println("[ESTOP] E-stop triggered — motors disabled. State → ESTOPPED.");
        }
        return;  // skip all other processing until reset
    }

    // ── Reset handling ────────────────────────────────────────────────────────
    if (g_resetReq) {
        g_resetReq = false;
        if (sysState == ESTOPPED) {
            enableMotors();
            sysState = UNINIT;
            Serial.println("[RESET] Motors re-enabled. State → UNINIT — re-trigger Z-switch to home.");
        } else {
            Serial.printf("[RESET] Ignored — not in ESTOPPED state (current: %s).\n",
                STATE_NAMES[sysState]);
        }
        return;
    }

    // ── Z-switch homing (only from UNINIT) ───────────────────────────────────
    if (sysState == UNINIT) {
        if (digitalRead(Z_SWITCH_PIN) == HIGH) {
            // Z = −FRAME_H: unit is at floor level (94 cm below pulleys).
            currentPos = { 100.0f, 100.0f, -FRAME_H };
            sysState   = HOMED;
            Serial.printf("[HOME] Z-switch triggered — homed to (%.1f, %.1f, %.1f). State → HOMED.\n",
                currentPos.x, currentPos.y, currentPos.z);
        }
        return;
    }

    // ── Go-to-Start request ───────────────────────────────────────────────────
    if (g_goToStartReq) {
        g_goToStartReq = false;
        if (sysState == HOMED) {
            // START_POSITION: near M1 pulley, 5 cm offset in XY to avoid zero-length
            // cables, −5 cm in Z (just below pulley level).
            // The unit travels from (100, 100, −94) upward and toward M1 corner.
            const Pos3D startPos = { 5.0f, 5.0f, -5.0f };
            sysState = MOVING;
            Serial.printf("[NAV] Go to Start — target (%.1f, %.1f, %.1f). State → MOVING.\n",
                startPos.x, startPos.y, startPos.z);
            beginMove(startPos);
        } else {
            Serial.printf("[NAV] Go to Start ignored — not in HOMED state (current: %s).\n",
                STATE_NAMES[sysState]);
        }
        return;
    }

    // ── Move execution (one Bresenham step per loop iteration) ───────────────
    if (sysState == MOVING) {
        if (stepMove()) {
            // stepMove() returned true — move is complete (or was aborted by estop).
            // Note: if estop caused the abort, g_estopReq will be true and will be
            // caught at the top of the NEXT loop() iteration.
            if (!g_estopReq) {
                sysState = AT_START;
                Serial.printf("[NAV] Arrived at start position (%.1f, %.1f, %.1f). State → AT_START.\n",
                    currentPos.x, currentPos.y, currentPos.z);
            }
        }
        return;
    }

    // AT_START and HOMED: nothing to do — wait for commands via HTTP.
}
