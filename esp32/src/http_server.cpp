/**
 * SpiderCam ESP32 – HTTP Server Routes
 *
 * All routes the Pi can call. Every handler reads the request body (JSON),
 * performs the action, and responds with a JSON body.
 *
 * Routes to implement:
 *
 *   GET  /ping
 *     Response: {"status": "ok"}
 *     Used by Pi on startup to confirm ESP32 is reachable before doing anything.
 *
 *   POST /move
 *     Body:     {"direction": "left"|"right"|"forward"|"backward"}
 *     Action:   moves STEPS_PER_MOVE steps in the given direction,
 *               updates position counter
 *     Response: {"ok": true, "x": <int>, "y": <int>}
 *
 *   POST /stop
 *     Action:   immediately halt all motors (disable step signal)
 *     Response: {"ok": true}
 *
 *   POST /start_inspection
 *     Action:   starts the autonomous inspection route (non-blocking if possible,
 *               or blocking — Pi will poll /status to know when it's done)
 *     Response: {"ok": true, "state": "running"}
 *
 *   GET  /position
 *     Response: {"x": <int>, "y": <int>}
 *     Pi calls this during a pass to tag each thermal frame with a coordinate.
 *
 *   GET  /status
 *     Response: {"state": "idle"|"moving"|"inspection", "x": <int>, "y": <int>}
 *
 * Claude Code instructions:
 *   - Use Arduino WebServer library (already available on ESP32 Arduino core)
 *   - Parse JSON with ArduinoJson library
 *   - Each handler calls into motor_control.h / position.h — keep handlers thin
 *   - Add CORS header to every response: server.sendHeader("Access-Control-Allow-Origin", "*")
 *     so the Pi's browser can call ESP32 directly if needed in future
 */

// #include <WebServer.h>
// #include <ArduinoJson.h>
// #include "config.h"
// #include "motor_control.h"
// #include "position.h"

// WebServer server(HTTP_PORT);

// void startHttpServer() { ... }
// void handlePing() { ... }
// void handleMove() { ... }
// void handleStop() { ... }
// void handleStartInspection() { ... }
// void handlePosition() { ... }
// void handleStatus() { ... }
