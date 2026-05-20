/**
 * SpiderCam ESP32 – Motor Control
 *
 * Drives stepper motors for X and Y axes of the gantry.
 * Direction mapping (adjust to match your physical wiring):
 *   "right"   → X+ (MOTOR_X_DIR_PIN HIGH, step MOTOR_X_STEP_PIN)
 *   "left"    → X- (MOTOR_X_DIR_PIN LOW,  step MOTOR_X_STEP_PIN)
 *   "forward" → Y+ (MOTOR_Y_DIR_PIN HIGH, step MOTOR_Y_STEP_PIN)
 *   "backward"→ Y- (MOTOR_Y_DIR_PIN LOW,  step MOTOR_Y_STEP_PIN)
 *
 * Functions to implement:
 *
 *   void initMotors()
 *     Set all motor pins as OUTPUT, set enable pin if used.
 *
 *   void moveSteps(int axis, int steps, bool dir)
 *     Blocking: pulse STEP_PIN `steps` times with STEP_DELAY_US between each.
 *     axis: 0=X, 1=Y. dir: true=positive, false=negative.
 *     Updates position counter via position.h after completing.
 *
 *   void stopMotors()
 *     Pull step pins LOW, optionally disable driver enable pin.
 *
 *   void runInspectionRoute()
 *     The autonomous inspection path. This is the full sweep the gantry makes.
 *     Implement as a sequence of moveSteps() calls that covers the inspection area.
 *     Return to home (0,0) at the end.
 *     Keep it interruptible: check a global `stopRequested` flag between moves.
 *
 * Claude Code instructions:
 *   - Keep moveSteps() simple and blocking for now (no acceleration ramp needed yet)
 *   - runInspectionRoute() will need tuning once the physical gantry dimensions are known
 *   - Document the step-to-mm conversion ratio once you measure it
 */

// #include "config.h"
// #include "position.h"

// void initMotors() { ... }
// void moveSteps(int axis, int steps, bool dir) { ... }
// void stopMotors() { ... }
// void runInspectionRoute() { ... }
