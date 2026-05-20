/**
 * SpiderCam – Manual Controls
 *
 * Claude Code instructions:
 *
 * 1. On page load, call GET /api/ping.
 *    If ok → set #connection-status to "online" class.
 *    If fail → set "offline" class and show warning.
 *
 * 2. D-pad buttons (data-direction attribute):
 *    - mousedown / touchstart → POST /api/move {"direction": btn.dataset.direction}
 *    - mouseup / touchend     → POST /api/stop
 *    Hold-to-move gives continuous movement while button is held.
 *
 * 3. #stop-btn → always POST /api/stop immediately on click.
 *
 * 4. #start-inspection-btn:
 *    - POST /api/inspection/start
 *    - Disable itself, enable #stop-inspection-btn
 *    - Update #inspection-status text
 *
 * 5. #stop-inspection-btn:
 *    - POST /api/inspection/stop
 *    - Re-enable #start-inspection-btn
 *
 * 6. SocketIO events to handle:
 *    "inspection_done"  → update #inspection-status, trigger results.js refresh
 *    "frame_captured"   → increment #frames-count
 *    "esp_disconnected" → update #connection-status, disable all move buttons
 */
