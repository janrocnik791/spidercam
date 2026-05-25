// =============================================================================
// v1_1_foundation.ino — ESP-hosted UI, manual motor jog, go-to-start
// WiFi AP: "SpiderCam"  →  open http://192.168.4.1 in any browser
// =============================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ─── WiFi ─────────────────────────────────────────────────────────────────────
const char* SSID = "SpiderCam";
WebServer server(80);

// ─── Pins ─────────────────────────────────────────────────────────────────────
const int STEP_PINS[4] = { 23, 17, 18, 21 };
const int DIR_PINS[4]  = { 22, 16,  5, 19 };

// ─── Timing ───────────────────────────────────────────────────────────────────
const unsigned int STEP_DELAY_US = 800;    // normal move speed
const unsigned int JOG_DELAY_US  = 3000;  // slower for manual jog
const int          JOG_STEPS     = 15;    // steps per jog HTTP request

// ─── Geometry ─────────────────────────────────────────────────────────────────
const float FRAME_W      = 200.0f;
const float FRAME_D      = 200.0f;
const float SPOOL_R      = 2.0f;
const float LINE_OFFSET  = 8.0f;
const float STEPS_PER_CM = 200.0f / (2.0f * (float)M_PI * SPOOL_R);

// ─── Positions (Z = 0 at pulleys, negative downward) ─────────────────────────
const float FROM_X = 100.0f, FROM_Y = 100.0f, FROM_Z = -94.0f;
const float TO_X   =   5.0f, TO_Y   =   5.0f, TO_Z   =  -5.0f;

// Per-motor step overrides: 0 = use geometry, non-zero = use this value
long stepOverride[4] = { 0, 0, 0, 0 };

// =============================================================================
// Geometry
// =============================================================================
float cable(int i, float x, float y, float z) {
    float mx = (i == 1 || i == 2) ? FRAME_W : 0.0f;
    float my = (i == 2 || i == 3) ? FRAME_D : 0.0f;
    float dx = x - mx, dy = y - my, dz = -z;
    return sqrtf(dx*dx + dy*dy + dz*dz) - LINE_OFFSET;
}

void resolveSteps(long out[4]) {
    for (int i = 0; i < 4; i++) {
        if (stepOverride[i] != 0) {
            out[i] = stepOverride[i];
        } else {
            float dl = cable(i, FROM_X, FROM_Y, FROM_Z)
                     - cable(i, TO_X,   TO_Y,   TO_Z);
            out[i] = (long)roundf(dl * STEPS_PER_CM);
        }
    }
}

// =============================================================================
// Motor moves
// =============================================================================
void jogMotor(int motor, bool windUp) {
    digitalWrite(DIR_PINS[motor], windUp ? HIGH : LOW);
    delayMicroseconds(20);
    for (int i = 0; i < JOG_STEPS; i++) {
        digitalWrite(STEP_PINS[motor], HIGH);
        delayMicroseconds(JOG_DELAY_US);
        digitalWrite(STEP_PINS[motor], LOW);
        delayMicroseconds(JOG_DELAY_US);
    }
}

void doGoToStart() {
    long steps[4]; resolveSteps(steps);
    long absSteps[4], maxSteps = 0;
    for (int i = 0; i < 4; i++) {
        absSteps[i] = labs(steps[i]);
        if (absSteps[i] > maxSteps) maxSteps = absSteps[i];
    }
    if (maxSteps == 0) return;

    for (int i = 0; i < 4; i++)
        digitalWrite(DIR_PINS[i], steps[i] >= 0 ? HIGH : LOW);
    delayMicroseconds(20);

    long acc[4] = {0,0,0,0};
    for (long ms = 0; ms < maxSteps; ms++) {
        bool go[4] = {false,false,false,false};
        for (int i = 0; i < 4; i++) {
            if (absSteps[i] == 0) continue;
            acc[i] += absSteps[i];
            if (acc[i] >= maxSteps) { acc[i] -= maxSteps; go[i] = true; }
        }
        for (int i = 0; i < 4; i++) if (go[i]) digitalWrite(STEP_PINS[i], HIGH);
        delayMicroseconds(STEP_DELAY_US);
        for (int i = 0; i < 4; i++) if (go[i]) digitalWrite(STEP_PINS[i], LOW);
        delayMicroseconds(STEP_DELAY_US);
        if (ms % 100 == 0) yield();
    }
}

// =============================================================================
// HTTP handlers
// =============================================================================
void handleJog() {
    int  m = server.arg("m").toInt();   // 0–3
    bool w = server.arg("w").toInt();   // 1=wind, 0=unwind
    if (m < 0 || m > 3) { server.send(400, "text/plain", "bad motor"); return; }
    jogMotor(m, w);
    server.send(200, "text/plain", "ok");
}

void handleGo() {
    long steps[4]; resolveSteps(steps);
    char msg[120];
    snprintf(msg, sizeof(msg),
        "Moving: M1=%ld M2=%ld M3=%ld M4=%ld",
        steps[0], steps[1], steps[2], steps[3]);
    Serial.printf("[GO] %s\n", msg);
    doGoToStart();
    Serial.println("[GO] Done.");
    server.send(200, "text/plain", msg);
}

// =============================================================================
// HTML UI
// =============================================================================
const char HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Argus V1.1</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
  body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
       background:#18181b;color:#f4f4f5;min-height:100vh;padding:16px}
  h1{font-size:18px;font-weight:700;letter-spacing:-.3px;margin-bottom:4px}
  #status{font-size:12px;color:#a1a1aa;margin-bottom:20px;min-height:16px}
  /* Go to start */
  #btn-go{width:100%;padding:16px;font-size:16px;font-weight:700;
          background:#2563eb;color:#fff;border:none;border-radius:10px;
          cursor:pointer;margin-bottom:24px;letter-spacing:.3px}
  #btn-go:active{background:#1d4ed8}
  #btn-go:disabled{background:#3f3f46;color:#71717a;cursor:not-allowed}
  /* Motor grid */
  .motors{display:flex;flex-direction:column;gap:10px}
  .motor{background:#27272a;border-radius:10px;padding:14px 16px}
  .motor-label{font-size:11px;font-weight:700;color:#71717a;
               text-transform:uppercase;letter-spacing:1px;margin-bottom:10px}
  .jog-row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
  .jog{padding:18px 8px;font-size:15px;font-weight:700;border:none;
       border-radius:8px;cursor:pointer;user-select:none;
       -webkit-user-select:none;width:100%}
  .wind  {background:#16a34a;color:#fff}
  .unwind{background:#dc2626;color:#fff}
  .wind:active  {background:#15803d}
  .unwind:active{background:#b91c1c}
  .jog:disabled{background:#3f3f46;color:#52525b;cursor:not-allowed}
</style>
</head>
<body>

<h1>Argus V1.1</h1>
<div id="status">Ready</div>

<button id="btn-go" onclick="goToStart()">Go to Start</button>

<div class="motors">
  <div class="motor">
    <div class="motor-label">Motor 1</div>
    <div class="jog-row">
      <button class="jog wind"   id="w0"
        onmousedown="startJog(0,1)" onmouseup="stopJog()" onmouseleave="stopJog()"
        ontouchstart="startJog(0,1);event.preventDefault()" ontouchend="stopJog()">&#8593; Wind</button>
      <button class="jog unwind" id="u0"
        onmousedown="startJog(0,0)" onmouseup="stopJog()" onmouseleave="stopJog()"
        ontouchstart="startJog(0,0);event.preventDefault()" ontouchend="stopJog()">&#8595; Unwind</button>
    </div>
  </div>
  <div class="motor">
    <div class="motor-label">Motor 2</div>
    <div class="jog-row">
      <button class="jog wind"   id="w1"
        onmousedown="startJog(1,1)" onmouseup="stopJog()" onmouseleave="stopJog()"
        ontouchstart="startJog(1,1);event.preventDefault()" ontouchend="stopJog()">&#8593; Wind</button>
      <button class="jog unwind" id="u1"
        onmousedown="startJog(1,0)" onmouseup="stopJog()" onmouseleave="stopJog()"
        ontouchstart="startJog(1,0);event.preventDefault()" ontouchend="stopJog()">&#8595; Unwind</button>
    </div>
  </div>
  <div class="motor">
    <div class="motor-label">Motor 3</div>
    <div class="jog-row">
      <button class="jog wind"   id="w2"
        onmousedown="startJog(2,1)" onmouseup="stopJog()" onmouseleave="stopJog()"
        ontouchstart="startJog(2,1);event.preventDefault()" ontouchend="stopJog()">&#8593; Wind</button>
      <button class="jog unwind" id="u2"
        onmousedown="startJog(2,0)" onmouseup="stopJog()" onmouseleave="stopJog()"
        ontouchstart="startJog(2,0);event.preventDefault()" ontouchend="stopJog()">&#8595; Unwind</button>
    </div>
  </div>
  <div class="motor">
    <div class="motor-label">Motor 4</div>
    <div class="jog-row">
      <button class="jog wind"   id="w3"
        onmousedown="startJog(3,1)" onmouseup="stopJog()" onmouseleave="stopJog()"
        ontouchstart="startJog(3,1);event.preventDefault()" ontouchend="stopJog()">&#8593; Wind</button>
      <button class="jog unwind" id="u3"
        onmousedown="startJog(3,0)" onmouseup="stopJog()" onmouseleave="stopJog()"
        ontouchstart="startJog(3,0);event.preventDefault()" ontouchend="stopJog()">&#8595; Unwind</button>
    </div>
  </div>
</div>

<script>
var jogTimer = null;
var busy = false;

function setStatus(t) { document.getElementById('status').textContent = t; }

function setAllDisabled(d) {
  document.getElementById('btn-go').disabled = d;
  document.querySelectorAll('.jog').forEach(function(b){ b.disabled = d; });
}

function startJog(m, w) {
  if (busy) return;
  sendJog(m, w);
  jogTimer = setInterval(function(){ sendJog(m, w); }, 160);
}

function stopJog() {
  if (jogTimer) { clearInterval(jogTimer); jogTimer = null; }
}

function sendJog(m, w) {
  if (busy) return;
  fetch('/jog?m=' + m + '&w=' + w, {method:'POST'}).catch(function(){});
}

function goToStart() {
  if (busy) return;
  busy = true;
  setAllDisabled(true);
  setStatus('Moving to start position…');
  fetch('/go', {method:'POST'})
    .then(function(r){ return r.text(); })
    .then(function(t){ setStatus('Done — ' + t); })
    .catch(function(){ setStatus('Error — check Serial Monitor'); })
    .finally(function(){ busy = false; setAllDisabled(false); });
}
</script>
</body>
</html>)html";

// =============================================================================
void setup() {
    // Disable hardware brownout detector.
    // WiFi radio init draws a ~300-500 mA spike; weak USB supplies sag below
    // the ~2.43 V BOD threshold and trigger a reset without this line.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    delay(500);

    for (int i = 0; i < 4; i++) {
        pinMode(STEP_PINS[i], OUTPUT);
        pinMode(DIR_PINS[i],  OUTPUT);
        digitalWrite(STEP_PINS[i], LOW);
        digitalWrite(DIR_PINS[i],  LOW);
    }

    // The WiFi driver spawns high-priority tasks on Core 0, starving its idle
    // task.  The default TWDT monitors both cores' idle tasks, so it fires
    // TG0WDT_SYS_RESET within ~3-5 s of softAP() being called.
    // disableCore0WDT() removes Core 0's idle task from TWDT monitoring
    // before the WiFi driver has a chance to starve it.
    disableCore0WDT();

    WiFi.softAP(SSID);
    Serial.printf("\n[v1_1] AP ready — SSID: %s  IP: %s\n",
        SSID, WiFi.softAPIP().toString().c_str());

    long steps[4]; resolveSteps(steps);
    Serial.printf("[v1_1] Computed steps: M1=%ld  M2=%ld  M3=%ld  M4=%ld\n",
        steps[0], steps[1], steps[2], steps[3]);

    server.on("/",    HTTP_GET,  []() { server.send_P(200, "text/html", HTML); });
    server.on("/jog", HTTP_POST, handleJog);
    server.on("/go",  HTTP_POST, handleGo);
    server.begin();

    Serial.println("[v1_1] Open http://192.168.4.1 in a browser.");
}

void loop() {
    server.handleClient();
    yield();
}
