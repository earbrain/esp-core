#include "earbrain/logging.hpp"
#include "earbrain/metrics.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <cstring>

static const char *TAG = "wifi_sta_example";

extern "C" void app_main(void) {
  earbrain::logging::info("=== WiFi Station Demo ===", TAG);

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

#if defined(CONFIG_WIFI_SSID) && defined(CONFIG_WIFI_PASSWORD)
  const char* ssid = CONFIG_WIFI_SSID;
  const char* password = CONFIG_WIFI_PASSWORD;

  earbrain::wifi().mode(earbrain::WifiMode::STA);
  if (strlen(ssid) > 0) {
    earbrain::logging::info("", TAG);
    earbrain::logging::infof(TAG, "Attempting STA connection to: %s", ssid);
    // Try to connect using provided credentials (do not perform scan here)
    earbrain::WifiCredentials creds{ssid, password};
    earbrain::wifi().connect(creds);
  } else {
    earbrain::logging::info("No WiFi credentials configured (use menuconfig or sdkconfig.local)", TAG);
    earbrain::logging::info("Started WiFi in STA mode without connection.", TAG);
  }
#else
  earbrain::wifi().mode(earbrain::WifiMode::STA);
  earbrain::logging::info("No WiFi credentials configured (use menuconfig or sdkconfig.local)", TAG);
  earbrain::logging::info("Started WiFi in STA mode without connection.", TAG);
#endif

  vTaskDelay(pdMS_TO_TICKS(2000));

  auto status = earbrain::wifi().status();
  const char* mode_str = (status.mode == earbrain::WifiMode::STA) ? "STA" :
                         (status.mode == earbrain::WifiMode::APSTA) ? "APSTA" : "Off";
  earbrain::logging::infof(TAG, "WiFi Mode: %s", mode_str);
  earbrain::logging::infof(TAG, "State: %d", static_cast<int>(status.state));
  earbrain::logging::infof(TAG, "Connected: %s", status.sta_connected ? "Yes" : "No");

  earbrain::logging::info("", TAG);
  earbrain::logging::info("Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
