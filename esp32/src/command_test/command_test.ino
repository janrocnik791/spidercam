// =============================================================================
// SpiderCam — command_test
//
// Minimal ESP32 sketch to test the Pi -> ESP32 command link. Connects to WiFi
// (station mode), starts an HTTP server on port 80, and echoes every received
// command to Serial so you can watch the Pi drive it.
//
// WiFi credentials live in secrets.h (gitignored). Open Serial Monitor at
// 115200 to see the assigned IP, then point the Pi's ESP client at it.
//
// Endpoints:
//   GET  /ping              -> (silent)       -> {"status":"ok"}
//   POST /start_inspection  -> [CMD] start    -> {"status":"ok"}
//   POST /pause             -> [CMD] pause    -> {"status":"ok"}
//   POST /resume            -> [CMD] resume   -> {"status":"ok"}
//   POST /estop             -> [CMD] ESTOP    -> {"status":"ok"}
//   POST /release           -> [CMD] release  -> {"status":"ok"}
//   POST /abort             -> [CMD] abort    -> {"status":"ok"}
//   GET  /position          ->                -> {"x":0,"y":0}
//   GET  /status            ->                -> {"state":"idle","x":0,"y":0}
//
// /ping is intentionally SILENT: the Pi polls it ~every 2 s as a reachability
// check, so printing it would flood the monitor and bury the real commands.
// =============================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_task_wdt.h"
#include "secrets.h"            // defines WIFI_SSID / WIFI_PASS (gitignored)

WebServer server(80);

// ── Reachability: static IP + mDNS ───────────────────────────────────────────
// The phone hotspot's DHCP can hand out a different address each boot, so pin a
// fixed one inside its /24 (192.168.85.0/24, observed from the prior lease).
// Point the Pi at it with:  ESP32_IP=192.168.85.85  (see pi/app/config.py).
// For local Pi<->ESP32 traffic only the subnet has to match; the gateway/DNS
// matter only for outbound internet, which this command server never needs.
IPAddress staticIP(192, 168, 85, 85);
IPAddress gateway (192, 168, 85, 1);
IPAddress subnet  (255, 255, 255, 0);
IPAddress dnsIP   (192, 168, 85, 1);

// mDNS hostname — matches the Pi's default ESP32_IP ("spidercam.local"), so the
// Pi resolves the board by name with zero config whenever the network passes
// mDNS multicast. The static IP above is the reliable fallback when it doesn't
// (phone hotspots often filter multicast).
const char *MDNS_HOST = "spidercam";   // -> spidercam.local

// ── command handlers ─────────────────────────────────────────────────────────
// Each command logs "[CMD] <label>" to Serial and replies {"status":"ok"}.
static void cmd(const char *label) {
    Serial.printf("[CMD] %s\n", label);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// /ping is silent — the Pi polls it ~0.5 Hz for reachability; printing it would
// flood the monitor. It still replies {"status":"ok"} so the Pi sees the link.
static void handlePing()            { server.send(200, "application/json", "{\"status\":\"ok\"}"); }
static void handleStartInspection() { cmd("start"); }
static void handlePause()           { cmd("pause"); }
static void handleResume()          { cmd("resume"); }
static void handleEstop()           { cmd("ESTOP"); }
static void handleRelease()         { cmd("release"); }   // E-stop released
static void handleAbort()           { cmd("abort"); }

static void handlePosition() {
    server.send(200, "application/json", "{\"x\":0,\"y\":0}");
}

static void handleStatus() {
    server.send(200, "application/json", "{\"state\":\"idle\",\"x\":0,\"y\":0}");
}

static void handleNotFound() {
    Serial.printf("[HTTP] 404 %s\n", server.uri().c_str());
    server.send(404, "application/json", "{\"status\":\"not_found\"}");
}

// ── WiFi ───────────────────────────────────────────────────────────────────--
static void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);                    // keep the radio responsive for HTTP
    if (!WiFi.config(staticIP, gateway, subnet, dnsIP)) {
        Serial.println("[WiFi] static IP config failed — falling back to DHCP");
    }
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi] Connecting to \"%s\"", WIFI_SSID);

    // Retry loop: dot per 500 ms; re-issue begin() every ~10 s until connected.
    unsigned long lastRetry = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - lastRetry > 10000) {
            Serial.print("[retry]");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            lastRetry = millis();
        }
    }
    Serial.printf("\n[WiFi] Connected! IP: %s  GW: %s  Mask: %s\n",
        WiFi.localIP().toString().c_str(),
        WiFi.gatewayIP().toString().c_str(),
        WiFi.subnetMask().toString().c_str());

    // (Re)announce mDNS after every (re)connection so spidercam.local tracks the
    // current link. Non-fatal: if it fails, the Pi can still use the static IP.
    MDNS.end();
    if (MDNS.begin(MDNS_HOST)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] reachable at http://%s.local\n", MDNS_HOST);
    } else {
        Serial.println("[mDNS] start failed (use the static IP instead)");
    }
}

// ── setup / loop ─────────────────────────────────────────────────────────────
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // disable brownout detector

    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[boot] command_test starting...");

    // The WiFi driver starves Core 0's idle task; the Task WDT watches both
    // cores' idle tasks by default and would fire TG0WDT_SYS_RESET a few seconds
    // after the radio comes up. Reconfigure the WDT to watch no idle task
    // (idle_core_mask = 0) — this avoids the reset and, unlike disableCore0WDT(),
    // leaves no dangling idle hook spamming "task_wdt ... task not found".
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,
        .trigger_panic = false,
    };
    esp_task_wdt_reconfigure(&wdt_config);

    connectWiFi();

    server.enableCORS(true);
    server.on("/ping",             HTTP_GET,  handlePing);
    server.on("/start_inspection", HTTP_POST, handleStartInspection);
    server.on("/pause",            HTTP_POST, handlePause);
    server.on("/resume",           HTTP_POST, handleResume);
    server.on("/estop",            HTTP_POST, handleEstop);
    server.on("/release",          HTTP_POST, handleRelease);
    server.on("/abort",            HTTP_POST, handleAbort);
    server.on("/position",         HTTP_GET,  handlePosition);
    server.on("/status",           HTTP_GET,  handleStatus);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.printf("[HTTP] Server ready at http://%s\n", WiFi.localIP().toString().c_str());
    Serial.println("[HTTP] Try: curl http://<ip>/ping");
}

void loop() {
    server.handleClient();

    // If WiFi drops, reconnect so the command link recovers on its own.
    static bool wasConnected = true;
    if (WiFi.status() != WL_CONNECTED) {
        if (wasConnected) { Serial.println("[WiFi] Lost connection — reconnecting..."); wasConnected = false; }
        connectWiFi();
        wasConnected = true;
    }
}
