// Spidercam 4-stepper ESP32 sketch
//
// This sketch reconstructs the geometry from the original Spidercam project,
// but drives all 4 DRV8825 stepper drivers directly with STEP/DIR pins on one
// ESP32. Update the measurement and calibration section below for your build.

#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "config.h"

// ---------------------------------------------------------------------------
// Stepper pins
// Kept the same as the working test_4_stepper sketch.
// ---------------------------------------------------------------------------
const int STEP_PIN_1 = 17;
const int DIR_PIN_1 = 16;

const int STEP_PIN_2 = 18;
const int DIR_PIN_2 = 5;

const int STEP_PIN_3 = 21;
const int DIR_PIN_3 = 19;

const int STEP_PIN_4 = 23;
const int DIR_PIN_4 = 22;

const int STEP_PINS[4] = {STEP_PIN_1, STEP_PIN_2, STEP_PIN_3, STEP_PIN_4};
const int DIR_PINS[4] = {DIR_PIN_1, DIR_PIN_2, DIR_PIN_3, DIR_PIN_4};

// ---------------------------------------------------------------------------
// Measurements and calibration to edit
// Units below are centimeters.
// ---------------------------------------------------------------------------
const float FRAME_WIDTH_CM = 40.0f;    // Distance from motor 1 side to motor 2 side
const float FRAME_DEPTH_CM = 30.4f;    // Distance from motor 3 side to motor 1 side
const float MAST_HEIGHT_CM = 34.0f;    // Motor height above the ground plane

// Radius of the spool / drum that winds the string.
const float SPOOL_RADIUS_CM = 1.5f;

// 1.8 degree motor = 200 full steps per revolution.
const int MOTOR_STEPS_PER_REV = 200;

// Change this if you later enable microstepping on the DRV8825.
// Examples: 1 = full step, 2 = half step, 4, 8, 16, 32...
const int MICROSTEPS = 1;

// Safe pulse timing for initial testing.
const unsigned int STEP_DELAY_US = 800;
const unsigned int DIR_SETUP_US = 20;

// Enter the carriage starting position you want the controller to assume at boot.
// This is important because cable length calculations are relative to the current
// estimated position.
const float START_X_CM = 20.0f;
const float START_Y_CM = 15.2f;
const float START_Z_CM = 10.0f;

// Enter a test target position here. The demo loop moves between START and TARGET.
const float TARGET_X_CM = 25.0f;
const float TARGET_Y_CM = 18.0f;
const float TARGET_Z_CM = 12.0f;

// Time to wait after each completed move.
const unsigned long HOLD_TIME_MS = 2000;

struct Position3D {
  float x;
  float y;
  float z;
};

struct CableLengths {
  float l1;
  float l2;
  float l3;
  float l4;
};

Position3D currentPosition = {START_X_CM, START_Y_CM, START_Z_CM};

WebServer server(HTTP_PORT);

// Limit switch pins (INPUT_PULLUP — LOW means switch triggered)
const int LIMIT_X_PIN = 32;
const int LIMIT_Y_PIN = 33;

// Movement state (written from Core 0 task, read from Core 1 server)
volatile bool isMoving = false;
volatile bool stopRequested = false;
Position3D pendingTarget = {START_X_CM, START_Y_CM, START_Z_CM};
TaskHandle_t motorTaskHandle = NULL;

bool isInsideWorkingVolume(const Position3D &position) {
  return position.x >= 0.0f && position.x <= FRAME_WIDTH_CM &&
         position.y >= 0.0f && position.y <= FRAME_DEPTH_CM &&
         position.z >= 0.0f && position.z <= MAST_HEIGHT_CM;
}

CableLengths calculateCableLengths(const Position3D &position) {
  // Geometry from the original Spidercam project:
  // Motor 1: (0, b, h)
  // Motor 2: (a, b, h)
  // Motor 3: (0, 0, h)
  // Motor 4: (a, 0, h)
  CableLengths lengths;

  const float dz = position.z - MAST_HEIGHT_CM;

  lengths.l1 = sqrtf((position.x * position.x) +
                     ((position.y - FRAME_DEPTH_CM) * (position.y - FRAME_DEPTH_CM)) +
                     (dz * dz));

  lengths.l2 = sqrtf(((position.x - FRAME_WIDTH_CM) * (position.x - FRAME_WIDTH_CM)) +
                     ((position.y - FRAME_DEPTH_CM) * (position.y - FRAME_DEPTH_CM)) +
                     (dz * dz));

  lengths.l3 = sqrtf((position.x * position.x) +
                     (position.y * position.y) +
                     (dz * dz));

  lengths.l4 = sqrtf(((position.x - FRAME_WIDTH_CM) * (position.x - FRAME_WIDTH_CM)) +
                     (position.y * position.y) +
                     (dz * dz));

  return lengths;
}

long deltaLengthCmToSteps(float deltaLengthCm) {
  const float spoolCircumferenceCm = 2.0f * PI * SPOOL_RADIUS_CM;
  const float stepsPerCm = (MOTOR_STEPS_PER_REV * MICROSTEPS) / spoolCircumferenceCm;
  return lroundf(deltaLengthCm * stepsPerCm);
}

void setMotorDirections(const long motorSteps[4]) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(DIR_PINS[i], motorSteps[i] >= 0 ? HIGH : LOW);
  }

  delayMicroseconds(DIR_SETUP_US);
}

void pulseMotor(int motorIndex) {
  digitalWrite(STEP_PINS[motorIndex], HIGH);
}

void endPulseMotor(int motorIndex) {
  digitalWrite(STEP_PINS[motorIndex], LOW);
}

void moveAllMotorsSynchronously(const long motorSteps[4]) {
  long remainingSteps[4];
  long maxSteps = 0;

  for (int i = 0; i < 4; i++) {
    remainingSteps[i] = labs(motorSteps[i]);
    if (remainingSteps[i] > maxSteps) {
      maxSteps = remainingSteps[i];
    }
  }

  if (maxSteps == 0) {
    return;
  }

  setMotorDirections(motorSteps);

  long accumulators[4] = {0, 0, 0, 0};

  for (long masterStep = 0; masterStep < maxSteps; masterStep++) {
    bool motorShouldStep[4] = {false, false, false, false};

    for (int i = 0; i < 4; i++) {
      if (remainingSteps[i] == 0) {
        continue;
      }

      accumulators[i] += remainingSteps[i];
      if (accumulators[i] >= maxSteps) {
        accumulators[i] -= maxSteps;
        motorShouldStep[i] = true;
      }
    }

    for (int i = 0; i < 4; i++) {
      if (motorShouldStep[i]) {
        pulseMotor(i);
      }
    }

    delayMicroseconds(STEP_DELAY_US);

    for (int i = 0; i < 4; i++) {
      if (motorShouldStep[i]) {
        endPulseMotor(i);
      }
    }

    delayMicroseconds(STEP_DELAY_US);
  }
}

void printCableLengths(const CableLengths &lengths) {
  Serial.print("L1: ");
  Serial.print(lengths.l1, 3);
  Serial.print(" cm, L2: ");
  Serial.print(lengths.l2, 3);
  Serial.print(" cm, L3: ");
  Serial.print(lengths.l3, 3);
  Serial.print(" cm, L4: ");
  Serial.print(lengths.l4, 3);
  Serial.println(" cm");
}

void printPosition(const char *label, const Position3D &position) {
  Serial.print(label);
  Serial.print(" X=");
  Serial.print(position.x, 2);
  Serial.print(" cm, Y=");
  Serial.print(position.y, 2);
  Serial.print(" cm, Z=");
  Serial.print(position.z, 2);
  Serial.println(" cm");
}

void moveToPosition(const Position3D &targetPosition) {
  if (!isInsideWorkingVolume(targetPosition)) {
    Serial.println("Target position is outside the configured working volume.");
    return;
  }

  CableLengths currentLengths = calculateCableLengths(currentPosition);
  CableLengths targetLengths = calculateCableLengths(targetPosition);

  float deltaLengths[4] = {
    targetLengths.l1 - currentLengths.l1,
    targetLengths.l2 - currentLengths.l2,
    targetLengths.l3 - currentLengths.l3,
    targetLengths.l4 - currentLengths.l4
  };

  long motorSteps[4] = {
    deltaLengthCmToSteps(deltaLengths[0]),
    deltaLengthCmToSteps(deltaLengths[1]),
    deltaLengthCmToSteps(deltaLengths[2]),
    deltaLengthCmToSteps(deltaLengths[3])
  };

  printPosition("Current:", currentPosition);
  printPosition("Target :", targetPosition);
  Serial.println("Current cable lengths:");
  printCableLengths(currentLengths);
  Serial.println("Target cable lengths:");
  printCableLengths(targetLengths);

  Serial.print("Delta lengths (cm): ");
  Serial.print(deltaLengths[0], 3);
  Serial.print(", ");
  Serial.print(deltaLengths[1], 3);
  Serial.print(", ");
  Serial.print(deltaLengths[2], 3);
  Serial.print(", ");
  Serial.println(deltaLengths[3], 3);

  Serial.print("Motor steps: ");
  Serial.print(motorSteps[0]);
  Serial.print(", ");
  Serial.print(motorSteps[1]);
  Serial.print(", ");
  Serial.print(motorSteps[2]);
  Serial.print(", ");
  Serial.println(motorSteps[3]);

  moveAllMotorsSynchronously(motorSteps);
  currentPosition = targetPosition;
  Serial.println("Move complete.");
  Serial.println();
}

void handlePing() {
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void motorTask(void *param) {
  moveToPosition(pendingTarget);
  isMoving = false;
  motorTaskHandle = NULL;
  vTaskDelete(NULL);
}

void homeAxes() {
  const float HOME_STEP_CM = 0.5f;
  const float HOME_Z_CM = START_Z_CM;

  Serial.println("Homing X axis...");
  while (digitalRead(LIMIT_X_PIN) == HIGH) {
    Position3D next = {currentPosition.x - HOME_STEP_CM,
                       currentPosition.y,
                       currentPosition.z};
    if (!isInsideWorkingVolume(next)) break;
    moveToPosition(next);
  }

  Serial.println("Homing Y axis...");
  while (digitalRead(LIMIT_Y_PIN) == HIGH) {
    Position3D next = {currentPosition.x,
                       currentPosition.y - HOME_STEP_CM,
                       currentPosition.z};
    if (!isInsideWorkingVolume(next)) break;
    moveToPosition(next);
  }

  // Z homing: placeholder for future limit switch on GPIO TBD
  currentPosition = {0.0f, 0.0f, HOME_Z_CM};
  Serial.println("Homing complete.");
}

void handleMove() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  if (!doc["x"].is<float>() || !doc["y"].is<float>() || !doc["z"].is<float>()) {
    server.send(400, "application/json", "{\"error\":\"missing x/y/z\"}");
    return;
  }
  Position3D target = {doc["x"].as<float>(),
                        doc["y"].as<float>(),
                        doc["z"].as<float>()};
  if (!isInsideWorkingVolume(target)) {
    server.send(400, "application/json", "{\"error\":\"out of bounds\"}");
    return;
  }
  if (isMoving) {
    server.send(409, "application/json", "{\"error\":\"busy\"}");
    return;
  }
  pendingTarget = target;
  isMoving = true;
  stopRequested = false;
  xTaskCreatePinnedToCore(motorTask, "motor", 4096, NULL, 1, &motorTaskHandle, 0);
  server.send(202, "application/json", "{\"status\":\"queued\"}");
}

void handleStop() {
  if (motorTaskHandle != NULL) {
    vTaskDelete(motorTaskHandle);
    motorTaskHandle = NULL;
    for (int i = 0; i < 4; i++) digitalWrite(STEP_PINS[i], LOW);
  }
  isMoving = false;
  stopRequested = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handlePosition() {
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}",
           currentPosition.x, currentPosition.y, currentPosition.z);
  server.send(200, "application/json", buf);
}

void handleStatus() {
  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"state\":\"%s\",\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}",
           isMoving ? "moving" : "idle",
           currentPosition.x, currentPosition.y, currentPosition.z);
  server.send(200, "application/json", buf);
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(STEP_PINS[i], OUTPUT);
    pinMode(DIR_PINS[i], OUTPUT);
    digitalWrite(STEP_PINS[i], LOW);
    digitalWrite(DIR_PINS[i], LOW);
  }

  delay(300);
  Serial.println();
  Serial.println("Spidercam 4-stepper ESP32 controller");
  Serial.println("Edit the measurement constants at the top of the sketch.");
  printPosition("Startup position:", currentPosition);
  Serial.println();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  MDNS.begin("spidercam");
  Serial.println("mDNS started — hostname: spidercam.local");

  pinMode(LIMIT_X_PIN, INPUT_PULLUP);
  pinMode(LIMIT_Y_PIN, INPUT_PULLUP);

  homeAxes();

  server.on("/ping",     HTTP_GET,  handlePing);
  server.on("/move",     HTTP_POST, handleMove);
  server.on("/stop",     HTTP_POST, handleStop);
  server.on("/position", HTTP_GET,  handlePosition);
  server.on("/status",   HTTP_GET,  handleStatus);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
