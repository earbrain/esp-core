#include "earbrain/logging.hpp"
#include "earbrain/mdns_service.hpp"
#include "earbrain/metrics.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_example";

extern "C" void app_main(void) {
  earbrain::logging::info("=== WiFi & mDNS Service Demo ===", TAG);

  earbrain::AccessPointConfig ap_config;
  ap_config.ssid = "esp-core-demo";
  ap_config.channel = 6;
  ap_config.auth_mode = WIFI_AUTH_OPEN;

  esp_err_t err = earbrain::wifi().start_access_point(ap_config);

  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to start AP: %s", esp_err_to_name(err));
    return;
  }

  earbrain::logging::infof(TAG, "AP started: %s", ap_config.ssid.c_str());

  vTaskDelay(pdMS_TO_TICKS(2000));

  auto status = earbrain::wifi().status();
  earbrain::logging::infof(TAG, "AP active: %s, STA active: %s",
                                 status.ap_active ? "Yes" : "No",
                                 status.sta_active ? "Yes" : "No");

  earbrain::logging::info("Performing WiFi scan...", TAG);
  auto scan_result = earbrain::wifi().perform_scan();

  if (scan_result.error == ESP_OK) {
    earbrain::logging::infof(TAG, "Found %zu networks", scan_result.networks.size());
    for (size_t i = 0; i < scan_result.networks.size() && i < 5; i++) {
      const auto &net = scan_result.networks[i];
      earbrain::logging::infof(TAG, "  %s (Signal: %d%%)", net.ssid.c_str(), net.signal);
    }
  }

  // Start mDNS service
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
    earbrain::logging::infof(TAG, "Discoverable as: %s.local", mdns_config.hostname.c_str());
    earbrain::logging::infof(TAG, "Service: %s%s:%u",
                                   mdns_config.service_type.c_str(),
                                   mdns_config.protocol.c_str(),
                                   mdns_config.port);
  } else {
    earbrain::logging::errorf(TAG, "Failed to start mDNS: %s", esp_err_to_name(err));
  }

  earbrain::logging::info("Demo completed. Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
