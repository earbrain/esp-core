#include "earbrain/logging.hpp"
#include "earbrain/metrics.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "smartconfig_example";

extern "C" void app_main(void) {
  earbrain::logging::info("=== SmartConfig Demo ===", TAG);
  earbrain::logging::info("", TAG);
  earbrain::logging::info("Instructions:", TAG);
  earbrain::logging::info("1. Install ESPTouch app on your smartphone", TAG);
  earbrain::logging::info("   - iOS: https://apps.apple.com/app/espressif-esptouch/id1071176700", TAG);
  earbrain::logging::info("   - Android: Search 'ESPTouch' on Google Play", TAG);
  earbrain::logging::info("2. Connect your phone to the WiFi network you want to configure", TAG);
  earbrain::logging::info("3. Open ESPTouch app and enter your WiFi password", TAG);
  earbrain::logging::info("4. Tap 'Confirm' to start provisioning", TAG);
  earbrain::logging::info("", TAG);

  // Start SmartConfig
  earbrain::logging::info("Starting SmartConfig...", TAG);
  esp_err_t err = earbrain::wifi().start_smart_config();

  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to start SmartConfig: %s", esp_err_to_name(err));
    return;
  }

  earbrain::logging::info("SmartConfig started successfully!", TAG);
  earbrain::logging::info("Waiting for WiFi credentials (timeout: 120 seconds)...", TAG);

  // Wait for SmartConfig to complete (120 seconds timeout)
  err = earbrain::wifi().wait_for_smart_config(120000);

  if (err == ESP_OK) {
    earbrain::logging::info("SmartConfig completed! Credentials received and connection validated.", TAG);

    // Stop SmartConfig (connection and saving happened automatically)
    earbrain::wifi().stop_smart_config();

    // Display connection info
    auto status = earbrain::wifi().status();
    if (status.sta_connected) {
      const ip4_addr_t *ip4 = reinterpret_cast<const ip4_addr_t *>(&status.sta_ip);
      char ip_buffer[16] = {0};
      ip4addr_ntoa_r(ip4, ip_buffer, sizeof(ip_buffer));

      earbrain::logging::info("Successfully connected to WiFi!", TAG);
      earbrain::logging::infof(TAG, "IP Address: %s", ip_buffer);

      // Show saved credentials (auto-saved after validation)
      auto credentials = earbrain::wifi().load_credentials();
      if (credentials.has_value()) {
        earbrain::logging::infof(TAG, "Saved SSID: %s", credentials->ssid.c_str());
      }
    }
  } else if (err == ESP_ERR_TIMEOUT) {
    earbrain::logging::warn("SmartConfig timed out. No credentials received.", TAG);
    earbrain::wifi().stop_smart_config();
  } else {
    earbrain::logging::errorf(TAG, "SmartConfig failed: %s", esp_err_to_name(err));
    earbrain::wifi().stop_smart_config();
  }

  earbrain::logging::info("Demo completed. Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
