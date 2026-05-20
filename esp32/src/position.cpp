/**
 * SpiderCam ESP32 – Position Tracker
 *
 * Maintains the current X-Y position of the gantry in step counts.
 * The Pi polls /position during an inspection pass to know which coordinate
 * to tag each thermal frame with.
 *
 * Global state:
 *   int posX = 0;   // steps from home in X axis
 *   int posY = 0;   // steps from home in Y axis
 *
 * Functions to implement:
 *
 *   void initPosition()
 *     Set posX = 0, posY = 0. Called at startup (assumes gantry starts at home).
 *     Later: add homing routine with limit switches.
 *
 *   void updatePosition(int axis, int steps, bool dir)
 *     Called by motor_control after each move.
 *     axis: 0=X, 1=Y. steps: how many. dir: true=+, false=-.
 *
 *   int getX()
 *   int getY()
 *     Simple getters used by http_server when building JSON responses.
 *
 * Note on coordinate system:
 *   Position is in raw step counts. The Pi's detection algorithm uses these
 *   as dictionary keys for frame lookup — so consistency matters more than
 *   physical units. If you need mm, add a STEPS_PER_MM constant in config.h
 *   and convert only at the API boundary.
 */

// int posX = 0;
// int posY = 0;

// void initPosition() { posX = 0; posY = 0; }
// void updatePosition(int axis, int steps, bool dir) { ... }
// int getX() { return posX; }
// int getY() { return posY; }
