// =============================================================================
// v1_3_foundation.ino  —  SpiderCam API server (no HTML, pure JSON)
// Connects to hotspot as STA. IP printed to serial on boot.
// Open spidercam_ui.html on any device on the same network, enter the IP.
// Serial: 115200 baud, Newline line ending.
// =============================================================================
//
//  SETUP:
//  1. Set WIFI_SSID / WIFI_PASS below.
//  2. Fill in CABLE_LEN with measured cable lengths (cm).
//  3. Set CM_PER_ROT = measured cm per spool rotation.
//  4. Flash. Open serial monitor — note the IP printed.
//  5. Open spidercam_ui.html, enter the IP, press Connect.
//
//  PARTITION: Tools → Partition Scheme → "Huge APP (3MB No OTA)"
// =============================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ─── WiFi ─────────────────────────────────────────────────────────────────────
const char* WIFI_SSID = "Jan's Galaxy S23";
const char* WIFI_PASS = "ubir0137";
WebServer server(80);

// ─── Pins ─────────────────────────────────────────────────────────────────────
const int STEP_PINS[4]    = { 23, 17, 18, 21 };
const int DIR_PINS[4]     = { 22, 16,  5, 19 };
const bool DIR_INVERT[4]  = { false, false, false, false };

// ─── Motor constants ──────────────────────────────────────────────────────────
const int STEPS_PER_REV = 200;
float CM_PER_ROT   = 12.7f;           // measured cm per spool rotation
float STEPS_PER_CM = 200.0f / 12.7f; // recomputed whenever CM_PER_ROT changes

unsigned int GO_DELAY_US  = 2500;     // µs per step during go/pass
unsigned int JOG_DELAY_US = 3000;     // µs per step during manual jog

// ─── Positions ────────────────────────────────────────────────────────────────
#define POS_HOME    0
#define POS_CORNER1 1
#define POS_CORNER2 2
#define POS_CORNER3 3
#define POS_CORNER4 4
#define NUM_POS     5
const char* POS_NAMES[NUM_POS] = { "home", "c1", "c2", "c3", "c4" };

// ─── Cable length table (cm) ──────────────────────────────────────────────────
//              M1       M2       M3       M4
float CABLE_LEN[NUM_POS][4] = {
    { 122.0f, 122.0f, 122.0f, 122.0f },  // HOME
    {   7.0f, 112.0f, 159.0f, 112.0f },  // CORNER1
    { 112.0f,   7.0f, 112.0f, 159.0f },  // CORNER2
    { 159.0f, 112.0f,   7.0f, 112.0f },  // CORNER3
    { 112.0f, 159.0f, 112.0f,   7.0f },  // CORNER4
};
float currentLen[4]; // tracked state, initialised from HOME on boot

// ─── Auto-pass ────────────────────────────────────────────────────────────────
#define MAX_PASS 20
int passSteps[MAX_PASS];
int passLen = 0;

// =============================================================================
// Motion helpers
// =============================================================================
void computeSteps(int posIdx, long out[4]) {
    for (int i = 0; i < 4; i++)
        out[i] = (long)roundf((currentLen[i] - CABLE_LEN[posIdx][i]) * STEPS_PER_CM);
}

void runMotor(int m, long steps, unsigned int us) {
    if (steps == 0) return;
    bool fwd = (steps > 0) ^ DIR_INVERT[m];
    digitalWrite(DIR_PINS[m], fwd ? HIGH : LOW);
    delayMicroseconds(20);
    long n = labs(steps);
    for (long i = 0; i < n; i++) {
        digitalWrite(STEP_PINS[m], HIGH); delayMicroseconds(us);
        digitalWrite(STEP_PINS[m], LOW);  delayMicroseconds(us);
        // No yield() — the loop task is not WDT-subscribed; WiFi runs on Core 0 independently
    }
}

void runAllBresenham(const long steps[4], unsigned int us) {
    long absS[4]; long maxS = 0;
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
    long acc[4] = {0,0,0,0};
    for (long t = 0; t < maxS; t++) {
        for (int i = 0; i < 4; i++) {
            if (!absS[i]) continue;
            acc[i] += absS[i];
            if (acc[i] >= maxS) {
                acc[i] -= maxS;
                digitalWrite(STEP_PINS[i], HIGH);
            }
        }
        delayMicroseconds(us);
        for (int i = 0; i < 4; i++) digitalWrite(STEP_PINS[i], LOW);
        delayMicroseconds(us);
        // No yield() — see runMotor comment
    }
}

// =============================================================================
// JSON builders
// =============================================================================
String buildStatus() {
    String j = "{\"current\":[";
    for (int i = 0; i < 4; i++) { j += String(currentLen[i], 1); if (i < 3) j += ','; }
    j += "],\"table\":{";
    for (int p = 0; p < NUM_POS; p++) {
        j += '"'; j += POS_NAMES[p]; j += "\":[";
        for (int i = 0; i < 4; i++) { j += String(CABLE_LEN[p][i], 1); if (i < 3) j += ','; }
        j += ']'; if (p < NUM_POS-1) j += ',';
    }
    j += "},\"cpr\":";   j += String(CM_PER_ROT, 2);
    j += ",\"spc\":";    j += String(STEPS_PER_CM, 4);
    j += ",\"go_us\":";  j += GO_DELAY_US;
    j += ",\"jog_us\":"; j += JOG_DELAY_US;
    j += '}';
    return j;
}

String buildPassStatus() {
    String j = "{\"pass\":[";
    for (int i = 0; i < passLen; i++) {
        j += '"'; j += POS_NAMES[passSteps[i]]; j += '"';
        if (i < passLen-1) j += ',';
    }
    j += "],\"len\":"; j += passLen;
    j += ",\"max\":"; j += MAX_PASS;
    j += '}';
    return j;
}

// =============================================================================
// CORS — call before every server.send()
// =============================================================================
void cors() {
    server.sendHeader("Access-Control-Allow-Origin",  "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// =============================================================================
// HTTP handlers
// Routes that accept POST are registered as HTTP_ANY so OPTIONS preflight works.
// Every handler calls cors() then checks for OPTIONS before doing real work.
// =============================================================================

void hStatus() {
    cors();
    server.send(200, "application/json", buildStatus());
}

void hGo() {
    cors();
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    String pos = server.arg("pos");
    int idx = -1;
    for (int p = 0; p < NUM_POS; p++) if (pos == POS_NAMES[p]) { idx = p; break; }
    if (idx < 0) { server.send(400, "text/plain", "unknown position"); return; }
    long st[4]; computeSteps(idx, st);
    Serial.printf("[GO→%s] M1=%ld M2=%ld M3=%ld M4=%ld\n", POS_NAMES[idx], st[0],st[1],st[2],st[3]);
    runAllBresenham(st, GO_DELAY_US);
    for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[idx][i];
    Serial.printf("[GO→%s] done\n", POS_NAMES[idx]);
    server.send(200, "application/json", buildStatus());
}

void hJog() {
    cors();
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    int m      = server.arg("m").toInt();
    float rot  = server.arg("rot").toFloat();
    if (m < 0 || m > 3 || rot == 0.0f) { server.send(400, "text/plain", "bad args"); return; }
    long steps = (long)roundf(rot * STEPS_PER_REV);
    runMotor(m, steps, JOG_DELAY_US);
    currentLen[m] -= (float)steps / STEPS_PER_CM;
    Serial.printf("[JOG M%d] %.2f rot  len=%.1f\n", m+1, rot, currentLen[m]);
    server.send(200, "application/json", buildStatus());
}

void hAnchor() {
    cors();
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[POS_HOME][i];
    Serial.printf("[ANCHOR] M1:%.1f M2:%.1f M3:%.1f M4:%.1f\n",
        currentLen[0],currentLen[1],currentLen[2],currentLen[3]);
    server.send(200, "application/json", buildStatus());
}

void hSetpos() {
    cors();
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    String pos = server.arg("pos");
    int idx = -1;
    for (int p = 0; p < NUM_POS; p++) if (pos == POS_NAMES[p]) { idx = p; break; }
    if (idx < 0) { server.send(400, "text/plain", "unknown position"); return; }
    for (int i = 0; i < 4; i++) CABLE_LEN[idx][i] = currentLen[i];
    Serial.printf("[SETPOS %s] M1:%.1f M2:%.1f M3:%.1f M4:%.1f\n",
        POS_NAMES[idx], currentLen[0],currentLen[1],currentLen[2],currentLen[3]);
    Serial.println("  Copy to CABLE_LEN in source to make permanent.");
    server.send(200, "application/json", buildStatus());
}

void hSpeed() {
    cors();
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    unsigned int us = (unsigned int)server.arg("us").toInt();
    if (us < 200 || us > 20000) { server.send(400, "text/plain", "range 200-20000"); return; }
    GO_DELAY_US = us; JOG_DELAY_US = us + 500;
    Serial.printf("[SPEED] GO=%u JOG=%u\n", GO_DELAY_US, JOG_DELAY_US);
    server.send(200, "application/json", buildStatus());
}

void hCpr() {
    cors();
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    float val = server.arg("val").toFloat();
    if (val <= 0) { server.send(400, "text/plain", "must be > 0"); return; }
    CM_PER_ROT   = val;
    STEPS_PER_CM = 200.0f / val;
    Serial.printf("[CPR] %.2f cm/rot → %.4f steps/cm\n", CM_PER_ROT, STEPS_PER_CM);
    server.send(200, "application/json", buildStatus());
}

void hPassStatus() {
    cors();
    server.send(200, "application/json", buildPassStatus());
}

void hPassAdd() {
    cors();
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    String pos = server.arg("pos");
    int idx = -1;
    for (int p = 0; p < NUM_POS; p++) if (pos == POS_NAMES[p]) { idx = p; break; }
    if (idx < 0 || passLen >= MAX_PASS) { server.send(400, "text/plain", "bad pos or full"); return; }
    passSteps[passLen++] = idx;
    Serial.printf("[PASS] added %s (%d/%d)\n", POS_NAMES[idx], passLen, MAX_PASS);
    server.send(200, "application/json", buildPassStatus());
}

void hPassClear() {
    cors();
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    passLen = 0;
    Serial.println("[PASS] cleared");
    server.send(200, "application/json", buildPassStatus());
}

void hPassRun() {
    cors();
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    if (passLen == 0) { server.send(400, "text/plain", "pass is empty"); return; }
    // Respond immediately so browser unblocks, then run motors
    server.send(200, "application/json", buildPassStatus());
    Serial.printf("[PASS] running %d steps\n", passLen);
    for (int s = 0; s < passLen; s++) {
        int idx = passSteps[s];
        long st[4]; computeSteps(idx, st);
        Serial.printf("[PASS %d/%d] → %s\n", s+1, passLen, POS_NAMES[idx]);
        runAllBresenham(st, GO_DELAY_US);
        for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[idx][i];
    }
    Serial.println("[PASS] done");
}

// =============================================================================
// Serial command handler
// =============================================================================
void handleSerial(String raw) {
    raw.trim();
    if (raw.length() == 0) return;

    if (raw == "s") {
        Serial.println("─────────────────────────────────────────────");
        Serial.printf("Current: M1:%.1f M2:%.1f M3:%.1f M4:%.1f\n",
            currentLen[0],currentLen[1],currentLen[2],currentLen[3]);
        for (int p = 0; p < NUM_POS; p++) {
            long st[4]; computeSteps(p, st);
            Serial.printf("  %-5s M1:%+ld M2:%+ld M3:%+ld M4:%+ld\n",
                POS_NAMES[p], st[0],st[1],st[2],st[3]);
        }
        Serial.printf("CPR:%.2f  SPC:%.4f  GO:%u  JOG:%u\n",
            CM_PER_ROT, STEPS_PER_CM, GO_DELAY_US, JOG_DELAY_US);
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.println("─────────────────────────────────────────────");
        return;
    }
    if (raw == "home") {
        for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[POS_HOME][i];
        Serial.printf("[ANCHOR] M1:%.1f M2:%.1f M3:%.1f M4:%.1f\n",
            currentLen[0],currentLen[1],currentLen[2],currentLen[3]);
        return;
    }
    if (raw.startsWith("go ")) {
        String t = raw.substring(3); t.trim();
        int idx = -1;
        for (int p = 0; p < NUM_POS; p++) if (t == POS_NAMES[p]) { idx = p; break; }
        if (idx < 0) { Serial.println("Valid: home c1 c2 c3 c4"); return; }
        long st[4]; computeSteps(idx, st);
        Serial.printf("[GO→%s] M1=%ld M2=%ld M3=%ld M4=%ld\n", POS_NAMES[idx],st[0],st[1],st[2],st[3]);
        runAllBresenham(st, GO_DELAY_US);
        for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[idx][i];
        Serial.printf("[GO→%s] done\n", POS_NAMES[idx]);
        return;
    }
    if (raw.startsWith("setpos ")) {
        String t = raw.substring(7); t.trim();
        int idx = -1;
        for (int p = 0; p < NUM_POS; p++) if (t == POS_NAMES[p]) { idx = p; break; }
        if (idx < 0) { Serial.println("Usage: setpos home|c1|c2|c3|c4"); return; }
        for (int i = 0; i < 4; i++) CABLE_LEN[idx][i] = currentLen[i];
        Serial.printf("[SETPOS %s] M1:%.1f M2:%.1f M3:%.1f M4:%.1f\n",
            POS_NAMES[idx], currentLen[0],currentLen[1],currentLen[2],currentLen[3]);
        return;
    }
    if (raw.startsWith("speed ")) {
        unsigned int us = (unsigned int)raw.substring(6).toInt();
        if (us < 200 || us > 20000) { Serial.println("Range: 200-20000"); return; }
        GO_DELAY_US = us; JOG_DELAY_US = us + 500;
        Serial.printf("[SPEED] GO=%u JOG=%u\n", GO_DELAY_US, JOG_DELAY_US);
        return;
    }
    if (raw.startsWith("cpr ") || raw.startsWith("spc ")) {
        float val = raw.substring(4).toFloat();
        if (val <= 0) { Serial.println("Usage: cpr <cm_per_rotation>"); return; }
        CM_PER_ROT = val; STEPS_PER_CM = 200.0f / val;
        Serial.printf("[CPR] %.2f cm/rot → %.4f steps/cm\n", CM_PER_ROT, STEPS_PER_CM);
        return;
    }
    if (raw.startsWith("all ")) {
        float rot = raw.substring(4).toFloat();
        if (rot == 0.0f) { Serial.println("Usage: all <rot>"); return; }
        long s = (long)roundf(rot * STEPS_PER_REV);
        long st[4] = {s,s,s,s};
        runAllBresenham(st, JOG_DELAY_US);
        for (int i = 0; i < 4; i++) currentLen[i] -= (float)s / STEPS_PER_CM;
        Serial.printf("[ALL] %.2f rot done\n", rot);
        return;
    }
    // Individual motor: "1 -0.5" etc.
    if (raw.length() >= 3 && raw[1] == ' ') {
        int m = raw[0] - '1';
        if (m < 0 || m > 3) { Serial.println("Unknown command."); return; }
        float rot = raw.substring(2).toFloat();
        if (rot == 0.0f) { Serial.printf("Usage: %c <rot>\n", raw[0]); return; }
        long st = (long)roundf(rot * STEPS_PER_REV);
        runMotor(m, st, JOG_DELAY_US);
        currentLen[m] -= (float)st / STEPS_PER_CM;
        Serial.printf("[M%d] done  len=%.1f\n", m+1, currentLen[m]);
        return;
    }
    // Pass commands
    if (raw == "pass" || raw == "pass show") {
        Serial.print("[PASS] ");
        for (int i = 0; i < passLen; i++) {
            Serial.print(POS_NAMES[passSteps[i]]);
            if (i < passLen-1) Serial.print(" → ");
        }
        if (passLen == 0) Serial.print("(empty)");
        Serial.printf(" (%d/%d)\n", passLen, MAX_PASS);
        return;
    }
    if (raw.startsWith("pass add ")) {
        String t = raw.substring(9); t.trim();
        int idx = -1;
        for (int p = 0; p < NUM_POS; p++) if (t == POS_NAMES[p]) { idx = p; break; }
        if (idx < 0 || passLen >= MAX_PASS) { Serial.println("Usage: pass add home|c1|c2|c3|c4"); return; }
        passSteps[passLen++] = idx;
        Serial.printf("[PASS] added %s (%d/%d)\n", POS_NAMES[idx], passLen, MAX_PASS);
        return;
    }
    if (raw == "pass clear") { passLen = 0; Serial.println("[PASS] cleared"); return; }
    if (raw == "pass run") {
        if (passLen == 0) { Serial.println("[PASS] empty"); return; }
        for (int s = 0; s < passLen; s++) {
            int idx = passSteps[s]; long st[4]; computeSteps(idx, st);
            Serial.printf("[PASS %d/%d] → %s\n", s+1, passLen, POS_NAMES[idx]);
            runAllBresenham(st, GO_DELAY_US);
            for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[idx][i];
        }
        Serial.println("[PASS] done");
        return;
    }
    Serial.println("Commands: s  home  go <pos>  setpos <pos>  1-4 <rot>  all <rot>  speed <us>  cpr <cm>  pass add/clear/run/show");
}

// =============================================================================
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);
    delay(500);

    for (int i = 0; i < 4; i++) {
        pinMode(STEP_PINS[i], OUTPUT);
        pinMode(DIR_PINS[i],  OUTPUT);
        digitalWrite(STEP_PINS[i], LOW);
        digitalWrite(DIR_PINS[i],  LOW);
    }
    for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[POS_HOME][i];

    // ── WiFi ────────────────────────────────────────────────────────────────
    disableCore0WDT();
    disableCore1WDT();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi] Connecting to \"%s\"", WIFI_SSID);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - t0 > 20000) {
            Serial.println("\n[WiFi] TIMEOUT — check SSID/password. Serial commands still work.");
            break;
        }
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.println("[WiFi] Open spidercam_ui.html, enter the IP above, press Connect.");
    }

    // ── Routes ──────────────────────────────────────────────────────────────
    // GET
    server.on("/status",      HTTP_GET,  hStatus);
    server.on("/pass/status", HTTP_GET,  hPassStatus);
    // POST (registered as HTTP_ANY to handle OPTIONS preflight)
    server.on("/go",          HTTP_ANY,  hGo);
    server.on("/jog",         HTTP_ANY,  hJog);
    server.on("/anchor",      HTTP_ANY,  hAnchor);
    server.on("/setpos",      HTTP_ANY,  hSetpos);
    server.on("/speed",       HTTP_ANY,  hSpeed);
    server.on("/cpr",         HTTP_ANY,  hCpr);
    server.on("/pass/add",    HTTP_ANY,  hPassAdd);
    server.on("/pass/clear",  HTTP_ANY,  hPassClear);
    server.on("/pass/run",    HTTP_ANY,  hPassRun);
    // Catch-all 404
    server.onNotFound([]() { cors(); server.send(404, "text/plain", "Not found"); });

    server.begin();
    Serial.printf("[v1_3] API ready. Calibration: %.2f cm/rot → %.4f steps/cm\n",
        CM_PER_ROT, STEPS_PER_CM);
}

void loop() {
    // Reconnect if hotspot restarts
    static unsigned long lastReconnect = 0;
    if (WiFi.status() != WL_CONNECTED && millis() - lastReconnect > 10000) {
        lastReconnect = millis();
        WiFi.reconnect();
    }
    server.handleClient();
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        handleSerial(cmd);
    }
    yield();
}
