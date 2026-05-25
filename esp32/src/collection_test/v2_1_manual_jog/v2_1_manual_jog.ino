// =============================================================================
// v2_1_manual_jog.ino — XY/Z jog + live position display
// No WiFi. Open Serial Monitor at 115200 baud, line ending: Newline.
// =============================================================================
//
// ── CALIBRATION ──────────────────────────────────────────────────────────────
//
//  1. Complete v1_2 calibration first:
//     a. Measure all 4 cable lengths at HOME and all 4 corners.
//        Fill in CABLE_LEN below.
//     b. Calibrate STEPS_PER_CM: send "1 1", measure cable travel in cm.
//        STEPS_PER_CM = 200 / <cm per rotation>
//  2. Measure FRAME_W and FRAME_D (pulley corner to pulley corner, in cm).
//     Fill in below. Only used for position display and jog direction — does
//     NOT affect the accuracy of 'go' commands.
//  3. Flash. Type 'home' then 's'. Check that computed XYZ matches the
//     physical home position visually.
//  4. Try 'jog x 10'. Compare position readout with a tape measure.
//     Fine-tune STEPS_PER_CM until position tracks correctly.
//
// ── COMMANDS ─────────────────────────────────────────────────────────────────
//
//   jog x <cm>   move ± cm in X direction (positive toward M2/M3 side)
//   jog y <cm>   move ± cm in Y direction (positive toward M3/M4 side)
//   jog z <cm>   move ± cm in Z direction (negative = down)
//
//   1 <rot>      rotate Motor 1   positive = wind (reel in)
//   2 <rot>      rotate Motor 2   negative = unwind (pay out)
//   3 <rot>      rotate Motor 3
//   4 <rot>      rotate Motor 4
//
//   all <rot>    rotate all 4 motors by the same amount simultaneously
//                (all of the above update the tracked cable lengths)
//
//   go home      move to HOME reference position
//   go c1        move to CORNER 1 (near M1)
//   go c2        move to CORNER 2 (near M2)
//   go c3        move to CORNER 3 (near M3)
//   go c4        move to CORNER 4 (near M4)
//
//   home         declare current physical position as HOME
//                (resets tracked cable lengths to HOME values)
//
//   s            show cable lengths, computed XYZ, and steps to all positions
//
// ── PULLEY LAYOUT ─────────────────────────────────────────────────────────────
//
//   M4 (0,D) ─────────── M3 (W,D)
//     |                     |
//     |                     |
//   M1 (0,0) ─────────── M2 (W,0)
//
//   Z = 0 at pulley level, negative = downward.
//   X positive = toward M2/M3.   Y positive = toward M3/M4.
//
// =============================================================================

#include <math.h>

// ─── Pins ─────────────────────────────────────────────────────────────────────
const int STEP_PINS[4] = { 23, 17, 18, 21 };
const int DIR_PINS[4]  = { 22, 16,  5, 19 };

// Per-motor direction invert. Set true for any motor whose physical winding
// direction is reversed relative to the software convention.
const bool DIR_INVERT[4] = { false, false, false, false };

// ─── Motor constants ──────────────────────────────────────────────────────────
const int          STEPS_PER_REV  = 200;    // full steps per revolution
const float        STEPS_PER_CM   = 15.92f; // calibrate: 200 / <cm per rotation>
const unsigned int STEP_DELAY_US  = 1500;   // manual jog speed
const unsigned int GO_DELAY_US    = 800;    // automated go speed

// ─── Frame geometry (for position display and jog commands only) ───────────────
const float FRAME_W = 200.0f;   // cm, M1→M2 and M4→M3
const float FRAME_D = 200.0f;   // cm, M1→M4 and M2→M3

// ─── Reference position indices ───────────────────────────────────────────────
#define POS_HOME    0
#define POS_CORNER1 1
#define POS_CORNER2 2
#define POS_CORNER3 3
#define POS_CORNER4 4
#define NUM_POS     5

const char* POS_NAMES[NUM_POS] = { "home", "c1", "c2", "c3", "c4" };

// ─── Cable length table ───────────────────────────────────────────────────────
// Measure every value (cm) at each physical reference position before flashing.
// Row = position index.  Col = motor index: 0=M1  1=M2  2=M3  3=M4
//
//                 M1       M2       M3       M4
const float CABLE_LEN[NUM_POS][4] = {
    {  0.0f,   0.0f,   0.0f,   0.0f },   // HOME    ← fill in
    {  0.0f,   0.0f,   0.0f,   0.0f },   // CORNER1 ← fill in
    {  0.0f,   0.0f,   0.0f,   0.0f },   // CORNER2 ← fill in
    {  0.0f,   0.0f,   0.0f,   0.0f },   // CORNER3 ← fill in
    {  0.0f,   0.0f,   0.0f,   0.0f },   // CORNER4 ← fill in
};

// ─── Tracked cable lengths ────────────────────────────────────────────────────
// Every motor command updates this. Initialised from HOME on boot.
float currentLen[4];

// =============================================================================
// Inverse kinematics — cable lengths → XYZ position
// =============================================================================
// Exact closed-form solution, no iteration.
//
// Pulley geometry gives:
//   R1² = x² + y² + z²
//   R2² = (x-W)² + y² + z²   →  subtract → x = (R1²-R2²+W²)/(2W)
//   R4² = x² + (y-D)² + z²   →  subtract → y = (R1²-R4²+D²)/(2D)
//   z   = -sqrt(R1²-x²-y²)   (negative = downward)
// =============================================================================
struct Vec3 { float x, y, z; };

Vec3 posFromCables() {
    float r1sq = currentLen[0] * currentLen[0];
    float r2sq = currentLen[1] * currentLen[1];
    float r4sq = currentLen[3] * currentLen[3];
    float x = (r1sq - r2sq + FRAME_W * FRAME_W) / (2.0f * FRAME_W);
    float y = (r1sq - r4sq + FRAME_D * FRAME_D) / (2.0f * FRAME_D);
    float zSq = r1sq - x * x - y * y;
    float z = (zSq > 0.0f) ? -sqrtf(zSq) : 0.0f;
    return {x, y, z};
}

// Forward kinematics — XYZ → cable lengths from each pulley corner.
void cableLensFromPos(float x, float y, float z, float out[4]) {
    for (int i = 0; i < 4; i++) {
        float px = (i == 1 || i == 2) ? FRAME_W : 0.0f;
        float py = (i == 2 || i == 3) ? FRAME_D : 0.0f;
        float dx = x - px, dy = y - py, dz = -z;   // dz always ≥ 0
        out[i] = sqrtf(dx*dx + dy*dy + dz*dz);
    }
}

// =============================================================================
// Step calculation from currentLen
// =============================================================================
void computeStepsToPos(const float targetLen[4], long out[4]) {
    for (int i = 0; i < 4; i++) {
        float dl = currentLen[i] - targetLen[i];
        // dl > 0: cable shortens → wind   → positive steps
        // dl < 0: cable lengthens → unwind → negative steps
        out[i] = (long)roundf(dl * STEPS_PER_CM);
    }
}

void computeStepsToRef(int posIdx, long out[4]) {
    computeStepsToPos(CABLE_LEN[posIdx], out);
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
    Vec3 pos = posFromCables();

    // Consistency check using M3 (not used in IK formula)
    float r3check = 0;
    {
        float dx = pos.x - FRAME_W, dy = pos.y - FRAME_D, dz = -pos.z;
        r3check = sqrtf(dx*dx + dy*dy + dz*dz);
    }
    float r3err = r3check - currentLen[2];

    Serial.println(F("─────────────────────────────────────────────────────────"));
    Serial.println(F("Cable lengths (cm):"));
    Serial.printf( "  M1: %6.1f   M2: %6.1f   M3: %6.1f   M4: %6.1f\n",
        currentLen[0], currentLen[1], currentLen[2], currentLen[3]);

    Serial.println(F("Computed position:"));
    Serial.printf( "  X: %.1f cm   Y: %.1f cm   Z: %.1f cm  (%.1f cm below pulleys)\n",
        pos.x, pos.y, pos.z, -pos.z);
    if (fabsf(r3err) > 2.0f) {
        Serial.printf( "  [!] M3 consistency check: computed %.1f cm vs tracked %.1f cm (err %.1f cm)\n",
            r3check, currentLen[2], r3err);
        Serial.println(F("      Large error suggests STEPS_PER_CM needs recalibration."));
    }

    Serial.println();
    Serial.println(F("Steps to reach each position:"));
    Serial.println(F("           M1        M2        M3        M4"));
    for (int p = 0; p < NUM_POS; p++) {
        long steps[4]; computeStepsToRef(p, steps);
        Serial.printf("  %-6s  %+8ld  %+8ld  %+8ld  %+8ld\n",
            POS_NAMES[p], steps[0], steps[1], steps[2], steps[3]);
    }
    Serial.println(F("  (+ = wind / reel in,   - = unwind / pay out)"));
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

    // ── home — re-anchor to HOME measured values ──────────────────────────────
    if (raw == "home") {
        for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[POS_HOME][i];
        Vec3 pos = posFromCables();
        Serial.printf("[HOME] anchored  M1:%.1f  M2:%.1f  M3:%.1f  M4:%.1f\n",
            currentLen[0], currentLen[1], currentLen[2], currentLen[3]);
        Serial.printf("[HOME] position  X:%.1f  Y:%.1f  Z:%.1f cm\n",
            pos.x, pos.y, pos.z);
        return;
    }

    // ── go <position> — move to a reference position ──────────────────────────
    if (raw.startsWith("go ")) {
        String target = raw.substring(3); target.trim();
        int posIdx = -1;
        for (int p = 0; p < NUM_POS; p++)
            if (target == POS_NAMES[p]) { posIdx = p; break; }
        if (posIdx < 0) {
            Serial.println("Unknown position. Valid: go home  go c1  go c2  go c3  go c4");
            return;
        }
        long steps[4]; computeStepsToRef(posIdx, steps);
        Serial.printf("[GO → %s]  M1=%ld  M2=%ld  M3=%ld  M4=%ld  t=%lums\n",
            POS_NAMES[posIdx], steps[0], steps[1], steps[2], steps[3], millis());
        runAllBresenham(steps, GO_DELAY_US);
        for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[posIdx][i];
        Vec3 pos = posFromCables();
        Serial.printf("[GO → %s] done  X:%.1f  Y:%.1f  Z:%.1f cm  t=%lums\n",
            POS_NAMES[posIdx], pos.x, pos.y, pos.z, millis());
        return;
    }

    // ── jog x/y/z <cm> — move in world coordinates ───────────────────────────
    if (raw.startsWith("jog ") && raw.length() >= 7) {
        char axis = raw[4];
        float cm = raw.substring(6).toFloat();
        if ((axis != 'x' && axis != 'y' && axis != 'z') || cm == 0.0f) {
            Serial.println("Usage: jog x <cm>  |  jog y <cm>  |  jog z <cm>");
            Serial.println("  positive X = toward M2/M3 side");
            Serial.println("  positive Y = toward M3/M4 side");
            Serial.println("  negative Z = downward");
            return;
        }

        Vec3 cur = posFromCables();
        Vec3 tgt = cur;
        if      (axis == 'x') tgt.x += cm;
        else if (axis == 'y') tgt.y += cm;
        else                  tgt.z += cm;

        float targetLen[4];
        cableLensFromPos(tgt.x, tgt.y, tgt.z, targetLen);

        long steps[4];
        computeStepsToPos(targetLen, steps);

        Serial.printf("[JOG %c %+.1f cm]  from (%.1f, %.1f, %.1f)\n",
            axis, cm, cur.x, cur.y, cur.z);
        Serial.printf("                  to   (%.1f, %.1f, %.1f)\n",
            tgt.x, tgt.y, tgt.z);
        Serial.printf("  steps  M1=%ld  M2=%ld  M3=%ld  M4=%ld  t=%lums\n",
            steps[0], steps[1], steps[2], steps[3], millis());

        runAllBresenham(steps, GO_DELAY_US);

        for (int i = 0; i < 4; i++) currentLen[i] = targetLen[i];
        Serial.printf("[JOG] done  t=%lums\n", millis());
        return;
    }

    // ── all <rot> — all motors same rotation ──────────────────────────────────
    if (raw.startsWith("all ")) {
        float rot = raw.substring(4).toFloat();
        if (rot == 0.0f) { Serial.println("Usage: all <rotations>  e.g. all -1.5"); return; }
        long s = (long)roundf(rot * STEPS_PER_REV);
        long steps[4] = {s, s, s, s};
        float deltaCm = -(float)s / STEPS_PER_CM;   // positive steps = shorter cable
        Serial.printf("[ALL] %.2f rot = %ld steps  (%s)  cable delta %.2f cm\n",
            rot, s, s >= 0 ? "wind" : "unwind", deltaCm);
        runAllBresenham(steps, STEP_DELAY_US);
        for (int i = 0; i < 4; i++) currentLen[i] += deltaCm;
        Vec3 pos = posFromCables();
        Serial.printf("[ALL] done  pos (%.1f, %.1f, %.1f) cm\n", pos.x, pos.y, pos.z);
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
        float deltaCm = -(float)steps / STEPS_PER_CM;   // positive steps = shorter cable
        Serial.printf("[M%d] %.2f rot = %ld steps  (%s)  cable delta %.2f cm  t=%lums\n",
            motorIdx+1, rot, steps, steps >= 0 ? "wind" : "unwind", deltaCm, millis());
        runMotor(motorIdx, steps, STEP_DELAY_US);
        currentLen[motorIdx] += deltaCm;
        Vec3 pos = posFromCables();
        Serial.printf("[M%d] done  M%d now %.1f cm  pos (%.1f, %.1f, %.1f) cm  t=%lums\n",
            motorIdx+1, motorIdx+1, currentLen[motorIdx],
            pos.x, pos.y, pos.z, millis());
        return;
    }

    Serial.println(F("Unknown command. Available commands:"));
    Serial.println(F("  s              show cable lengths, position, and pending steps"));
    Serial.println(F("  home           re-anchor tracked cables to HOME values"));
    Serial.println(F("  go home        move to HOME reference position"));
    Serial.println(F("  go c1..c4      move to corner 1–4 reference position"));
    Serial.println(F("  jog x <cm>     move ± cm in X  (toward M2/M3 = positive)"));
    Serial.println(F("  jog y <cm>     move ± cm in Y  (toward M3/M4 = positive)"));
    Serial.println(F("  jog z <cm>     move ± cm in Z  (downward = negative)"));
    Serial.println(F("  1..4 <rot>     rotate one motor  (+ wind / - unwind)"));
    Serial.println(F("  all <rot>      rotate all motors the same amount"));
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

    for (int i = 0; i < 4; i++) currentLen[i] = CABLE_LEN[POS_HOME][i];

    Serial.println(F("\n[v2_1] Ready."));
    Serial.println(F("  *** Fill in CABLE_LEN table and set FRAME_W/D before use! ***"));
    printStatus();
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        handleCommand(cmd);
    }
}
