#include "earbrain/logging.hpp"
#include "earbrain/metrics.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <cstring>

// Try to include credentials.h if it exists, otherwise fall back to sdkconfig
#if __has_include("../credentials.h")
#include "../credentials.h"
#endif

static const char *TAG = "wifi_sta_example";

extern "C" void app_main(void) {
  earbrain::logging::info("=== WiFi Station Demo ===", TAG);

  // Initialize WiFi service
  esp_err_t err = earbrain::wifi().initialize();
  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
    return;
  }

  earbrain::wifi().on([](const earbrain::WifiEventData& event) {
    switch (event.event) {
      case earbrain::WifiEvent::Connected:
        if (event.ip_address.has_value()) {
          earbrain::logging::infof(TAG, "Connected! IP Address: %s",
                                   earbrain::ip_to_string(event.ip_address.value()).c_str());
        }
        break;

      case earbrain::WifiEvent::Disconnected:
        if (event.disconnect_reason.has_value()) {
          earbrain::logging::warnf(TAG, "Disconnected (reason=%d)",
                                   static_cast<int>(event.disconnect_reason.value()));
        }
        break;

      case earbrain::WifiEvent::ConnectionFailed:
        earbrain::logging::errorf(TAG, "Connection failed: %s",
                                  esp_err_to_name(event.error_code));
        break;

      default:
        break;
    }
  });

  // Save credentials from credentials.h if available
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  earbrain::logging::infof(TAG, "Saving credentials for: %s", WIFI_SSID);
  esp_err_t save_err = earbrain::wifi().save_credentials(WIFI_SSID, WIFI_PASSWORD);
  if (save_err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to save credentials: %s", esp_err_to_name(save_err));
  }
#endif

  // Start STA mode (will auto-connect if credentials are saved)
  earbrain::wifi().mode(earbrain::WifiMode::STA);

  vTaskDelay(pdMS_TO_TICKS(2000));

  auto status = earbrain::wifi().status();
  const char* mode_str = (status.mode == earbrain::WifiMode::STA) ? "STA" :
                         (status.mode == earbrain::WifiMode::APSTA) ? "APSTA" : "Off";
  earbrain::logging::infof(TAG, "WiFi Mode: %s", mode_str);
  earbrain::logging::infof(TAG, "Connected: %s", status.sta_connected ? "Yes" : "No");
  earbrain::logging::infof(TAG, "Connecting: %s", status.sta_connecting ? "Yes" : "No");

  earbrain::logging::info("", TAG);
  earbrain::logging::info("Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
