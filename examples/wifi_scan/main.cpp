#include "earbrain/logging.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "esp_wifi.h"
#include <cstring>

static const char *TAG = "wifi_scan_example";

extern "C" void app_main(void) {
  earbrain::logging::info("=== WiFi Scan Only Demo ===", TAG);

  // Initialize WiFi service
  esp_err_t err = earbrain::wifi().initialize();
  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
    return;
  }

  // Optional: listen for Wi-Fi events just to log basic state changes
  earbrain::wifi().on([](const earbrain::WifiEventData &event) {
    switch (event.event) {
      case earbrain::WifiEvent::Connected:
        earbrain::logging::info("WiFi connected (STA)", TAG);
        break;
      case earbrain::WifiEvent::Disconnected:
        earbrain::logging::info("WiFi disconnected (STA)", TAG);
        break;
      default:
        break;
    }
  });

  // Put WiFi into STA mode without attempting to connect
  earbrain::logging::info("Starting WiFi in STA mode (no auto-connect)", TAG);
  earbrain::wifi().mode(earbrain::WifiMode::STA);

  // Ensure we are not in the middle of connecting. In IDF v5.2, there is no
  // esp_wifi_set_auto_connect API, so just disconnect proactively to avoid any
  // ongoing connection attempt if credentials exist in NVS.
  esp_wifi_disconnect();

  // Give the driver a moment to settle
  vTaskDelay(pdMS_TO_TICKS(300));

  earbrain::logging::info("Performing WiFi scan...", TAG);
  auto scan_result = earbrain::wifi().perform_scan();

  if (scan_result.error == ESP_OK) {
    earbrain::logging::infof(TAG, "Found %zu networks:", scan_result.networks.size());

    const size_t max_show = 20;
    for (size_t i = 0; i < scan_result.networks.size() && i < max_show; i++) {
      const auto &net = scan_result.networks[i];
      const char *auth_mode = "Unknown";
      switch (net.auth_mode) {
        case WIFI_AUTH_OPEN: auth_mode = "Open"; break;
        case WIFI_AUTH_WEP: auth_mode = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: auth_mode = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK: auth_mode = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: auth_mode = "WPA/WPA2"; break;
        case WIFI_AUTH_WPA3_PSK: auth_mode = "WPA3"; break;
        default: break;
      }
      earbrain::logging::infof(TAG, "  [%zu] %s", i + 1, net.ssid.c_str());
      earbrain::logging::infof(TAG, "      RSSI: %d dBm | Signal: %d%% | Ch: %d | Auth: %s",
                               net.rssi, net.signal, net.channel, auth_mode);
    }
    if (scan_result.networks.size() > max_show) {
      earbrain::logging::infof(TAG, "... and %zu more networks",
                               scan_result.networks.size() - max_show);
    }
  } else {
    earbrain::logging::errorf(TAG, "Scan failed: %s", esp_err_to_name(scan_result.error));
  }

  earbrain::logging::info("Scan complete. Going to idle loop...", TAG);

  // Idle loop to keep app running
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
