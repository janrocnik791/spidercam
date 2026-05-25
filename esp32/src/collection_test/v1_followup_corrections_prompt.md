# Claude Code Follow-Up Prompt — v1 Corrections

This prompt amends the v1_foundation.ino implementation with two critical corrections
derived from the verified test_all_motors sketch. Apply these changes before anything else.

---

## Context

The file `test_all_motors/test_all_motors.ino` was the physical ground truth used to verify
motor behaviour on this machine. Its pin assignments are authoritative:

```cpp
// test_all_motors — verified working
STEP_PINS = {23, 17, 18, 21};   // motors [0..3]
DIR_PINS  = {22, 16,  5, 19};   // motors [0..3]
```

From physical observation on the actual assembly:
- `DIR = HIGH` (forward / `stepAll(true)`)  → all 4 cables **reel in** → unit moves **UP**
- `DIR = LOW`  (backward / `stepAll(false)`) → all 4 cables **pay out** → unit moves **DOWN**

---

## Correction 1 — Motor Step Sign Is Inverted

### Problem
In the current geometry code, `moveToPosition()` computes:

```cpp
deltaLengths[i] = targetLength - currentLength;
motorSteps[i]   = deltaLengthCmToSteps(deltaLengths[i]);
// then: DIR = HIGH when motorSteps >= 0
```

This is backwards. A **positive** delta means the cable must get **longer** (pay out,
DIR=LOW). But the code sets DIR=HIGH for positive steps — the opposite of what the
hardware does.

### Fix
Negate the delta before converting to steps, so that:
- cable needs to SHORTEN (negative delta) → positive steps → DIR=HIGH → reel in ✓
- cable needs to LENGTHEN (positive delta) → negative steps → DIR=LOW  → pay out ✓

```cpp
// WRONG (original):
motorSteps[i] = deltaLengthCmToSteps(targetLength - currentLength);

// CORRECT:
motorSteps[i] = deltaLengthCmToSteps(currentLength - targetLength);
```

Apply this fix inside `moveToPosition()` for all 4 motors.

---

## Correction 2 — Z Axis Convention

### Definition
Z = 0 is at the **pulley level** (top of the frame).
Z is **negative** going downward. The full working range is Z = 0 (top) to Z = −94 cm (bottom).

This matches the physical observation: moving to start means travelling **upward** from
the homed centre position, which is Z = −94, toward the M1 pulley at Z ≈ 0.

### Homed position (centre level-switch triggered)
```cpp
// When GPIO 33 goes HIGH, record:
currentPosition = { 100.0f, 100.0f, -94.0f };
```

### Start position (near M1 pulley)
```cpp
// Safe near-M1 position — 5 cm offset in XY and Z to avoid zero-length cables:
const Position3D START_POSITION = { 5.0f, 5.0f, -5.0f };
```
"Go to Start" moves the unit from (100, 100, −94) to (5, 5, −5).
This is an upward move of ~89 cm in Z plus a diagonal shift in XY.

### Cable length formula — no change needed
The formula `sqrt(dx² + dy² + z²)` is still correct because z² is always positive
regardless of the sign of z. No change required to `calculateCableLengths()`.

### Working volume bounds check — update Z range
```cpp
// WRONG (original assumed Z positive):
position.z >= 0.0f && position.z <= MAST_HEIGHT_CM

// CORRECT:
position.z >= -FRAME_H && position.z <= 0.0f
// where FRAME_H = 94.0f
```

### Serial / UI display
Anywhere the Z coordinate is displayed, show it as a positive "depth below pulleys"
value for human readability:

```cpp
float displayZ = fabsf(currentPosition.z);   // e.g. −47.0 → shows as 47.0 cm below top
```

---

## Summary of changes

| Location                  | Change                                                          |
|---------------------------|-----------------------------------------------------------------|
| `moveToPosition()`        | Flip step sign: use `currentLength - targetLength`             |
| Homed position constant   | `{ 100.0f, 100.0f, -94.0f }`                                   |
| `START_POSITION` constant | `{ 5.0f, 5.0f, -5.0f }`                                        |
| `isInsideWorkingVolume()` | Z bounds: `z >= -94.0f && z <= 0.0f`                           |
| UI / Serial Z display     | Show `fabsf(z)` with label "cm below top" to avoid confusion   |

No other changes to v1_foundation.ino are required.
After applying these corrections, the "Go to Start" sequence should physically move the
unit upward and toward the M1 corner, which is the observable verification test.
