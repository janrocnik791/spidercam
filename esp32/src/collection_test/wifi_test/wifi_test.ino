// Minimal ESP32 WiFi AP test.
// Upload, open Serial Monitor at 115200, look for the IP address.

#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_task_wdt.h"

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting AP...");

    // The WiFi driver spawns high-priority tasks on Core 0 that starve its idle
    // task. The Task WDT watches both cores' idle tasks by default, so it fires
    // TG0WDT_SYS_RESET ~3-5 s after softAP(). The Arduino disableCore0WDT()
    // helper stops the reset but, on this core (3.x / IDF 5.x), only deletes the
    // idle task while leaving its idle hook registered — which then floods the
    // log with "task_wdt: esp_task_wdt_reset(): task not found". Instead,
    // reconfigure the Task WDT to watch no idle task at all: no reset, no spam.
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,   // don't subscribe either core's idle task
        .trigger_panic = false,
    };
    esp_task_wdt_reconfigure(&wdt_config);

    WiFi.softAP("SpiderCam", "12345678");

    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Done. Connect to SpiderCam wifi.");
}

void loop() {}
