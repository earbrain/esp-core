#include "earbrain/logging.hpp"
#include "earbrain/metrics.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_ap_example";

extern "C" void app_main(void) {
  earbrain::logging::info("=== WiFi Access Point Demo ===", TAG);

  earbrain::AccessPointConfig ap_config;
  ap_config.ssid = "esp-core-demo";
  ap_config.channel = 6;
  ap_config.auth_mode = WIFI_AUTH_OPEN;

  earbrain::logging::infof(TAG, "Starting AP: %s", ap_config.ssid.c_str());
  esp_err_t err = earbrain::wifi().start_apsta(ap_config);

  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to start AP: %s", esp_err_to_name(err));
    return;
  }

  earbrain::logging::info("AP started successfully!", TAG);

  vTaskDelay(pdMS_TO_TICKS(2000));

  auto status = earbrain::wifi().status();
  earbrain::logging::infof(TAG, "AP active: %s", status.ap_active ? "Yes" : "No");
  earbrain::logging::infof(TAG, "STA active: %s", status.sta_active ? "Yes" : "No");

  earbrain::logging::info("", TAG);
  earbrain::logging::info("Access Point is running. Connect to it using:", TAG);
  earbrain::logging::infof(TAG, "  SSID: %s", ap_config.ssid.c_str());
  earbrain::logging::info("  Password: (none - open network)", TAG);

  earbrain::logging::info("Demo completed. Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
