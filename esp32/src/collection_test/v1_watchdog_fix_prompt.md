# Claude Code Prompt — Fix ESP32 Watchdog Reset in v1_foundation.ino

## Problem

The ESP32 crashes with `rst:0x7 (TG0WDT_SYS_RESET)` shortly after boot.
This is a Task Watchdog Timer (TWDT) reset — it fires when the main Arduino task
does not yield to the FreeRTOS scheduler for more than ~3–5 seconds.
The same crash happened in the earlier full version of the sketch, which means the
root cause is shared code — almost certainly the blocking stepper loop.

---

## Step 1 — Diagnose: find every place that blocks without yielding

Search `v1_foundation.ino` for all of the following patterns and list them with line numbers:

1. Any loop that calls `delayMicroseconds()` or `delay()` in a tight iteration
   (e.g. the `moveAllMotorsSynchronously()` / stepping loop)
2. Any `while(true)` or `for(long i = 0; i < maxSteps; i++)` loop that contains
   no `yield()`, `vTaskDelay()`, `esp_task_wdt_reset()`, or `delay()` call
3. Any blocking wait in `setup()` (e.g. waiting for WiFi, waiting for a pin)
4. Any `while (!condition) {}` poll loop with no yield inside

The most likely culprit is the motor stepping loop: it can run for thousands of
iterations of paired `delayMicroseconds(800)` calls. A 200-step-per-rev motor
moving 94 cm on a 40 mm spool needs ~2400 steps — that loop takes ~4 seconds
without ever returning to the scheduler, which is longer than the watchdog timeout.

---

## Step 2 — Fix the stepping loop (primary fix)

### Option A — Feed the watchdog periodically (quickest fix)

Add `#include "esp_task_wdt.h"` at the top of the file.

Inside `moveAllMotorsSynchronously()`, call `esp_task_wdt_reset()` every 100 master
steps to tell the watchdog the task is still alive:

```cpp
for (long masterStep = 0; masterStep < maxSteps; masterStep++) {

    // ... existing Bresenham logic and pulse code ...

    if (masterStep % 100 == 0) {
        esp_task_wdt_reset();   // keep watchdog happy during long moves
    }
}
```

### Option B — Non-blocking stepping (better long-term, required for v3+)

Replace the blocking loop with a state-machine stepper that executes ONE master tick
per `loop()` call and returns immediately. This is the correct architecture for
keeping the web server and WebSocket responsive during movement.

Implement two functions:

```cpp
// Call once to configure a move; returns immediately.
void beginMove(const Position3D &target);

// Call from loop() — executes one Bresenham tick, returns true when move is done.
bool stepMove();
```

Store all loop state (remainingSteps, accumulators, masterStep, maxSteps,
motorShouldStep) as file-scope variables so they persist between `loop()` calls.

In `loop()`:
```cpp
if (isMoving) {
    if (stepMove()) {
        isMoving = false;
        onMoveComplete();   // advance state machine
    }
}
server.handleClient();      // always runs, even mid-move
```

**Option B is strongly preferred** — it also fixes any future WebSocket / UI
freeze during movement. Implement Option B if the refactor is straightforward;
fall back to Option A only if the non-blocking refactor would require changes
beyond the stepping and state-machine code.

---

## Step 3 — Fix any blocking waits in setup()

If `setup()` contains anything like:

```cpp
while (WiFi.status() != WL_CONNECTED) { delay(500); }
```

Replace with a non-blocking approach or add a timeout so setup() always completes
within 2 seconds. In AP mode (which this sketch uses) WiFi.softAP() is synchronous
and fast — no polling loop should be needed.

---

## Step 4 — Increase watchdog timeout as a safety net (optional, belt-and-suspenders)

At the top of `setup()`, extend the watchdog timeout to 10 seconds so incidental
short blocks don't crash the system during development:

```cpp
#include "esp_task_wdt.h"
// In setup(), before anything else:
esp_task_wdt_init(10, true);   // 10-second timeout, panic on trigger
esp_task_wdt_add(NULL);        // watch the current (main) task
```

---

## Deliverable

Modify `v1_foundation.ino` in-place. After the fix:
- The sketch must not reset during a "Go to Start" move (the longest expected move,
  ~94 cm upward from the homed centre position).
- The web UI must remain reachable at 192.168.4.1 throughout the move.
- Print a Serial message at the start and end of every move so timing can be verified.
