#pragma once

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID     "Jan's Tab S8+"
#define WIFI_PASSWORD "jwcv2644"

// ── HTTP server ───────────────────────────────────────────────────────────────
#define HTTP_PORT 80

// ── Motor pins ────────────────────────────────────────────────────────────────
// Fill these in once you know your wiring.
// Example for a stepper with A4988 driver:
#define MOTOR_X_STEP_PIN  14
#define MOTOR_X_DIR_PIN   12
#define MOTOR_Y_STEP_PIN  27
#define MOTOR_Y_DIR_PIN   26

// ── Onboard LED (used for connection feedback) ────────────────────────────────
#define LED_PIN 2   // GPIO2 on most ESP32 dev boards

