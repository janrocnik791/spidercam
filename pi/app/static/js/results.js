/**
 * SpiderCam – Leak Detection Results
 *
 * Claude Code instructions:
 *
 * 1. Listen for SocketIO "inspection_done" event.
 *    When received, call loadResults().
 *
 * 2. loadResults():
 *    GET /api/results/latest
 *    If alerts.length === 0 → show "No anomalies detected ✓" in #alerts-container
 *    If alerts.length > 0 → render alert cards:
 *
 *    Each card shows:
 *      - Coordinate: X={x}, Y={y}
 *      - Max delta: {max_delta}°C
 *      - Affected pixels: {affected_pixels}
 *      - Diff heatmap: <img src="data:image/png;base64,{diff_image}">
 *
 *    Show #confirm-baseline-btn.
 *
 * 3. #confirm-baseline-btn click:
 *    POST /api/results/confirm-baseline {"inspection_id": current_inspection_id}
 *    On success: hide button, show "Baseline updated ✓"
 */
