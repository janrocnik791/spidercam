// =============================================================================
// v1_2_foundation.ino — Serial motor control, measured cable lengths
// No WiFi. Open Serial Monitor at 115200 baud, line ending: Newline.
// =============================================================================
//
// ── CALIBRATION ──────────────────────────────────────────────────────────────
//
//  1. Home the unit (hang it at the known home position).
//  2. Measure each cable from pulley to camera mount with a tape measure.
//  3. Fill in row 0 (HOME) of CABLE_LEN below.
//  4. Move camera to corner 1 (near M1 pulley), measure all 4 cables.
//  5. Fill in row 1 (CORNER1). Repeat for corners 2–4.
//  6. Calibrate: send "1 1" (one full rotation on any motor).
//     Measure how many cm of cable moved.
//     Set CM_PER_ROT = <that number>  — no math needed, division is automatic.
//     Or tune live without reflashing: cpr <measured_cm>
//  7. Re-flash, open Serial Monitor, type 's' to verify calibration line and
//     step counts look sensible before running any 'go' command.
//
// ── COMMANDS ─────────────────────────────────────────────────────────────────
//
//   1 <rot>     rotate Motor 1   positive = wind (reel in)
//   2 <rot>     rotate Motor 2   negative = unwind (pay out)
//   3 <rot>     rotate Motor 3
//   4 <rot>     rotate Motor 4
//
//   all <rot>   rotate all 4 motors by the same amount simultaneously
//
//   go home     move to HOME position
//   go c1       move to CORNER 1 (near M1)
//   go c2       move to CORNER 2 (near M2)
//   go c3       move to CORNER 3 (near M3)
//   go c4       move to CORNER 4 (near M4)
//
//   home        declare current physical position as HOME
//               (resets tracked cable lengths to HOME values)
//
//   s              show current cable lengths, calibration, and steps to every position
//   setpos <name>  record current cable lengths as a reference position
//                  (jog camera to the right spot first, then type setpos home / c1 / c2 …)
//   speed <us>     set move speed live — bigger = slower (wire jumping → try 3000–5000)
//   cpr <cm>       set cm-per-rotation live — type your measured value directly
//
// NOTE: moving directly corner-to-corner (e.g. c1→c2) will cause a Z drop
//       mid-move (cable-space path ≠ straight Cartesian path).
//       Workaround until waypoints are implemented: go home between corners.
//
// =============================================================================

#include <math.h>

// ─── Pins ─────────────────────────────────────────────────────────────────────
const int STEP_PINS[4] = { 23, 17, 18, 21 };
const int DIR_PINS[4]  = { 22, 16,  5, 19 };

// Per-motor direction invert. Flip any entry to true if that motor's
// physical winding direction is reversed relative to the others.
const bool DIR_INVERT[4] = { false, false, false, false };

// ─── Motor constants ──────────────────────────────────────────────────────────
const int STEPS_PER_REV = 200;          // full steps per revolution

// ── Spool calibration ─────────────────────────────────────────────────────────
// Measure: send "1 1" (one full rotation), measure cable travel in cm.
// Enter ONLY that raw measurement below — the division happens automatically.
// Tune live without reflashing: cpr <measured_cm>
float CM_PER_ROT   = 12.7f;                      // ← your measured cm per rotation
float STEPS_PER_CM = 200.0f / CM_PER_ROT;        // computed — do not edit directly

// Speed — larger value = slower.  Slowing GO prevents wire jumping off spool.
//   Tune live with "speed <us>" command without reflashing.
//   Typical range: 1500 (fast) – 4000 (slow).
unsigned int JOG_DELAY_US = 3000;   // single-motor manual commands
unsigned int GO_DELAY_US  = 2500;   // automated go / Bresenham moves

// ─── Reference position indices ───────────────────────────────────────────────
#define POS_HOME    0
#define POS_CORNER1 1   // near M1 pulley  (0,   0)
#define POS_CORNER2 2   // near M2 pulley  (W,   0)
#define POS_CORNER3 3   // near M3 pulley  (W,   D)
#define POS_CORNER4 4   // near M4 pulley  (0,   D)
#define NUM_POS     5

const char* POS_NAMES[NUM_POS] = { "home", "c1", "c2", "c3", "c4" };

// ─── Cable length table ───────────────────────────────────────────────────────
// Can be updated at runtime with "setpos <name>" — physically jog the camera
// to the right spot then type setpos to record actual cable lengths there.
// Row  = position index.   Col  = motor index  0=M1  1=M2  2=M3  3=M4
//
//              M1       M2       M3       M4
float CABLE_LEN[NUM_POS][4] = {
    {  122.0f,   122.0f,   122.0f,   122.0f },   // HOME    ← fill in
    {  7.0f,   112.0f,   159.0f,   112.0f },   // CORNER1 ← fill in
    {  112.0f,   7.0f,   112.0f,   159.0f },   // CORNER2 ← fill in
    {  159.0f,   112.0f,   7.0f,   112.0f },   // CORNER3 ← fill in
    {  112.0f,   159.0f,   112.0f,   7.0f },   // CORNER4 ← fill in
};

// ─── Tracked cable lengths ────────────────────────────────────────────────────
// Initialised from HOME on boot. Updated to target values after each 'go'.
// Manual single-motor jogs do NOT update this — use 'home' to re-anchor.
float currentLen[4];

// =============================================================================
// Step calculation
// =============================================================================

// Compute steps to move from currentLen to the given reference position.
// Positive steps = wind (shorten cable) = DIR HIGH
// Negative steps = unwind (lengthen cable) = DIR LOW
void computeSteps(int posIdx, long out[4]) {
    for (int i = 0; i < 4; i++) {
        float dl = currentLen[i] - CABLE_LEN[posIdx][i];
        out[i] = (long)roundf(dl * STEPS_PER_CM);
    }
}

// =============================================================================
// Motor moves
// =============================================================================

void runMotor(int motor, long steps, unsigned int delayUs) {
    if (steps == 0) return;
    bool fwd = (steps > 0) ^ DIR_INVERT[motor];
    digitalWrite(DIR_PINS[motor], fwd ? HIGH : LOW);
    delayMicroseconds(20);
    long count = labs(steps);
    for (long i = 0; i < count; i++) {
        digitalWrite(STEP_PINS[motor], HIGH);
        delayMicroseconds(delayUs);
        digitalWrite(STEP_PINS[motor], LOW);
        delayMicroseconds(delayUs);
        if (i % 100 == 0) yield();
    }
}

void runAllBresenham(const long steps[4], unsigned int delayUs) {
    long absS[4], maxS = 0;
    for (int i = 0; i < 4; i++) {
        absS[i] = labs(steps[i]);
        if (absS[i] > maxS) maxS = absS[i];
    }
    if (maxS == 0) { Serial.println("  (nothing to do — all steps are 0)"); return; }

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
        if (t % 100 == 0) yield();
    }
}

// =============================================================================
// Print status
// =============================================================================
void printStatus() {
    Serial.println(F("─────────────────────────────────────────────────────────"));
    Serial.println(F("Current cable lengths (cm):"));
    Serial.printf( "  M1: %.1f   M2: %.1f   M3: %.1f   M4: %.1f\n",
        currentLen[0], currentLen[1], currentLen[2], currentLen[3]);
    Serial.println();
    Serial.println(F("Steps to reach each position:"));
    Serial.println(F("         M1        M2        M3        M4"));
    for (int p = 0; p < NUM_POS; p++) {
        long steps[4]; computeSteps(p, steps);
        Serial.printf("  %-6s %+8ld  %+8ld  %+8ld  %+8ld\n",
            POS_NAMES[p], steps[0], steps[1], steps[2], steps[3]);
    }
    Serial.println(F("  (+ = wind / reel in,  - = unwind / pay out)"));
    Serial.printf( "Calibration: %.2f cm/rot  →  %.4f steps/cm   GO=%uµs  JOG=%uµs\n",
        CM_PER_ROT, STEPS_PER_CM, GO_DELAY_US, JOG_DELAY_US);
    Serial.println(F("─────────────────────────────────────────────────────────"));
}

// =============================================================================
// Command parser
// =============================================================================
void handleCommand(String raw) {
    raw.trim();
    if (raw.length() == 0) return;

    // ── s — show status ───────────────────────────────────────────────────────
    if (raw == "s") {
        printStatus();
        return;
    }

    // ── home — re-anchor tracked position to HOME values ──────────────────────
    if (raw == "home") {
        for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[POS_HOME][i];
        Serial.printf("[HOME] tracked lengths reset to  M1:%.1f  M2:%.1f  M3:%.1f  M4:%.1f\n",
            currentLen[0], currentLen[1], currentLen[2], currentLen[3]);
        return;
    }

    // ── go <position> — move to a reference position ──────────────────────────
    if (raw.startsWith("go ")) {
        String target = raw.substring(3);
        target.trim();
        int posIdx = -1;
        for (int p = 0; p < NUM_POS; p++) {
            if (target == POS_NAMES[p]) { posIdx = p; break; }
        }
        if (posIdx < 0) {
            Serial.println("Unknown position. Valid: go home  go c1  go c2  go c3  go c4");
            return;
        }

        long steps[4]; computeSteps(posIdx, steps);
        Serial.printf("[GO → %s]  M1=%ld  M2=%ld  M3=%ld  M4=%ld  t=%lums\n",
            POS_NAMES[posIdx], steps[0], steps[1], steps[2], steps[3], millis());

        runAllBresenham(steps, GO_DELAY_US);

        // Update tracked lengths to the position we just moved to
        for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[posIdx][i];
        Serial.printf("[GO → %s] done  t=%lums\n", POS_NAMES[posIdx], millis());
        return;
    }

    // ── all <rot> — all motors same rotation ──────────────────────────────────
    if (raw.startsWith("all ")) {
        float rot = raw.substring(4).toFloat();
        if (rot == 0.0f) { Serial.println("Usage: all <rotations>  e.g. all -1.5"); return; }
        long s = (long)roundf(rot * STEPS_PER_REV);
        long steps[4] = {s, s, s, s};
        Serial.printf("[ALL] %.2f rot = %ld steps  (%s)\n",
            rot, s, s >= 0 ? "wind" : "unwind");
        runAllBresenham(steps, JOG_DELAY_US);
        Serial.println("[ALL] done  (tracked lengths not updated — use 'home' to re-anchor)");
        return;
    }

    // ── <1-4> <rot> — single motor ────────────────────────────────────────────
    if (raw.length() >= 3 && raw[1] == ' ') {
        int motorIdx = raw[0] - '1';
        if (motorIdx < 0 || motorIdx > 3) {
            Serial.println("Unknown command. Type 's' to show status.");
            return;
        }
        float rot = raw.substring(2).toFloat();
        if (rot == 0.0f) {
            Serial.printf("Usage: %c <rotations>  e.g. %c -1.5\n", raw[0], raw[0]);
            return;
        }
        long steps = (long)roundf(rot * STEPS_PER_REV);
        Serial.printf("[M%d] %.2f rot = %ld steps  (%s)  t=%lums\n",
            motorIdx+1, rot, steps, steps >= 0 ? "wind" : "unwind", millis());
        runMotor(motorIdx, steps, JOG_DELAY_US);
        Serial.printf("[M%d] done  t=%lums\n", motorIdx+1, millis());
        return;
    }

    // ── setpos <name> — record currentLen as a reference position ────────────
    if (raw.startsWith("setpos ")) {
        String name = raw.substring(7); name.trim();
        int posIdx = -1;
        for (int p = 0; p < NUM_POS; p++)
            if (name == POS_NAMES[p]) { posIdx = p; break; }
        if (posIdx < 0) {
            Serial.println("Usage: setpos <name>   valid names: home  c1  c2  c3  c4");
            Serial.println("  Jog camera to the correct position first, then run setpos.");
            return;
        }
        for (int i = 0; i < 4; i++) CABLE_LEN[posIdx][i] = currentLen[i];
        Serial.printf("[SETPOS %s]  M1:%.1f  M2:%.1f  M3:%.1f  M4:%.1f\n",
            POS_NAMES[posIdx],
            CABLE_LEN[posIdx][0], CABLE_LEN[posIdx][1],
            CABLE_LEN[posIdx][2], CABLE_LEN[posIdx][3]);
        Serial.println("  Update CABLE_LEN in source with these values to make permanent.");
        return;
    }

    // ── speed <us> — set go speed live ───────────────────────────────────────
    if (raw.startsWith("speed ")) {
        unsigned int us = (unsigned int)raw.substring(6).toInt();
        if (us < 200 || us > 20000) {
            Serial.println("Usage: speed <microseconds>  e.g. speed 2500");
            Serial.println("  Range 200 (fast) – 20000 (very slow). Wire jumping → try 3000–5000.");
            return;
        }
        GO_DELAY_US = us;
        JOG_DELAY_US = us + 500;   // jog always slightly slower than go
        Serial.printf("[SPEED] GO=%u µs  JOG=%u µs  (not saved — reflash to make permanent)\n",
            GO_DELAY_US, JOG_DELAY_US);
        return;
    }

    // ── cpr <cm> — set cm-per-rotation live (type your measured value directly) ─
    if (raw.startsWith("cpr ") || raw.startsWith("spc ")) {
        float val = raw.substring(4).toFloat();
        if (val <= 0.0f) {
            Serial.println("Usage: cpr <cm_per_rotation>  e.g. cpr 12.7");
            Serial.println("  Send '1 1', measure cable travel in cm, type that number here.");
            return;
        }
        CM_PER_ROT   = val;
        STEPS_PER_CM = 200.0f / CM_PER_ROT;
        Serial.printf("[CPR] %.2f cm/rot  →  %.4f steps/cm\n", CM_PER_ROT, STEPS_PER_CM);
        Serial.println("      Update CM_PER_ROT in source to make permanent.");
        return;
    }

    Serial.println(F("Unknown command. Available commands:"));
    Serial.println(F("  s              show cable lengths and steps to all positions"));
    Serial.println(F("  home           re-anchor tracked position to HOME"));
    Serial.println(F("  go home        move to HOME"));
    Serial.println(F("  go c1..c4      move to corner 1–4"));
    Serial.println(F("  1..4 <rot>     rotate one motor  (+ wind / - unwind)"));
    Serial.println(F("  all <rot>      rotate all motors the same amount"));
    Serial.println(F("  setpos <name>  record current cables as reference position (home/c1..c4)"));
    Serial.println(F("  speed <us>     set move speed in µs/step  (bigger = slower)"));
    Serial.println(F("  cpr <cm>       set cm-per-rotation (type your measured value directly)"));
}

// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    for (int i = 0; i < 4; i++) {
        pinMode(STEP_PINS[i], OUTPUT);
        pinMode(DIR_PINS[i],  OUTPUT);
        digitalWrite(STEP_PINS[i], LOW);
        digitalWrite(DIR_PINS[i],  LOW);
    }

    // Initialise tracked position from HOME cable lengths
    for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[POS_HOME][i];

    Serial.println(F("\n[v1_2] Ready."));
    Serial.println(F("  *** Fill in CABLE_LEN table before using go commands! ***"));
    printStatus();
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        handleCommand(cmd);
    }
}
