#include "earbrain/logging.hpp"
#include "earbrain/mdns_service.hpp"
#include "earbrain/metrics.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mdns_example";

extern "C" void app_main(void) {
  earbrain::logging::info("=== mDNS Service Demo ===", TAG);

  // Start WiFi in AP mode (mDNS needs network interface)
  earbrain::logging::info("Starting WiFi AP (required for mDNS)...", TAG);
  earbrain::AccessPointConfig ap_config;
  ap_config.ssid = "esp-core-mdns";
  ap_config.channel = 6;
  ap_config.auth_mode = WIFI_AUTH_OPEN;

  auto config = earbrain::wifi().config();
  config.ap_config = ap_config;

  esp_err_t err = earbrain::wifi().set_config(config);
  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
    return;
  }

  err = earbrain::wifi().mode(earbrain::WifiMode::APSTA);
  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to set mode to APSTA: %s", esp_err_to_name(err));
    return;
  }

  earbrain::logging::info("AP started successfully!", TAG);

  vTaskDelay(pdMS_TO_TICKS(2000));

  // Start mDNS service
  earbrain::logging::info("", TAG);
  earbrain::logging::info("Starting mDNS service...", TAG);
  earbrain::MdnsConfig mdns_config;
  mdns_config.hostname = "esp-core-device";
  mdns_config.instance_name = "ESP Core Demo";
  mdns_config.service_type = "_http";
  mdns_config.protocol = "_tcp";
  mdns_config.port = 80;

  err = earbrain::mdns().start(mdns_config);

  if (err == ESP_OK) {
    earbrain::logging::info("mDNS started successfully!", TAG);
    earbrain::logging::info("", TAG);
    earbrain::logging::info("Device is now discoverable as:", TAG);
    earbrain::logging::infof(TAG, "  Hostname: %s.local", mdns_config.hostname.c_str());
    earbrain::logging::infof(TAG, "  Service: %s%s:%u",
                                   mdns_config.service_type.c_str(),
                                   mdns_config.protocol.c_str(),
                                   mdns_config.port);
    earbrain::logging::info("", TAG);
    earbrain::logging::info("You can discover this device using:", TAG);
    earbrain::logging::info("  - macOS/Linux: dns-sd -B _http._tcp", TAG);
    earbrain::logging::info("  - iOS: Download Discovery - DNS-SD Browser app", TAG);
    earbrain::logging::info("  - Android: Download BonjourBrowser app", TAG);
  } else {
    earbrain::logging::errorf(TAG, "Failed to start mDNS: %s", esp_err_to_name(err));
  }

  earbrain::logging::info("", TAG);
  earbrain::logging::info("Demo completed. Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
