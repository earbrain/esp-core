#include "earbrain/logging.hpp"
#include "earbrain/metrics.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <cstring>

static const char *TAG = "wifi_sta_example";

extern "C" void app_main(void) {
  earbrain::logging::info("=== WiFi Station & Scan Demo ===", TAG);

  // Connect to WiFi if credentials are provided
#if defined(CONFIG_WIFI_SSID) && defined(CONFIG_WIFI_PASSWORD)
  const char* ssid = CONFIG_WIFI_SSID;
  const char* password = CONFIG_WIFI_PASSWORD;

  if (strlen(ssid) > 0) {
    earbrain::logging::info("", TAG);
    earbrain::logging::infof(TAG, "Starting WiFi and connecting to: %s", ssid);

    earbrain::WifiCredentials credentials;
    credentials.ssid = ssid;
    credentials.passphrase = password;

    // Start WiFi and connect (all in one step)
    esp_err_t err = earbrain::wifi().start(credentials);

    if (err == ESP_OK) {
      earbrain::logging::info("Successfully connected to WiFi!", TAG);

      auto status = earbrain::wifi().status();
      if (status.sta_connected) {
        const ip4_addr_t *ip4 = reinterpret_cast<const ip4_addr_t *>(&status.sta_ip);
        char ip_buffer[16] = {0};
        ip4addr_ntoa_r(ip4, ip_buffer, sizeof(ip_buffer));
        earbrain::logging::infof(TAG, "IP Address: %s", ip_buffer);
      }
    } else {
      earbrain::logging::errorf(TAG, "Failed to connect: %s", esp_err_to_name(err));
    }
  } else {
    earbrain::logging::info("No WiFi credentials configured (use menuconfig or sdkconfig.local)", TAG);
    earbrain::logging::info("Starting WiFi without connection...", TAG);
    esp_err_t err = earbrain::wifi().start();
    if (err != ESP_OK) {
      earbrain::logging::errorf(TAG, "Failed to start station: %s", esp_err_to_name(err));
      return;
    }
  }
#else
  earbrain::logging::info("No WiFi credentials configured (use menuconfig or sdkconfig.local)", TAG);
  earbrain::logging::info("Starting WiFi without connection...", TAG);
  esp_err_t err = earbrain::wifi().start();
  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to start station: %s", esp_err_to_name(err));
    return;
  }
#endif

  vTaskDelay(pdMS_TO_TICKS(1000));

  auto status = earbrain::wifi().status();
  earbrain::logging::infof(TAG, "AP active: %s", status.ap_active ? "Yes" : "No");
  earbrain::logging::infof(TAG, "STA active: %s", status.sta_active ? "Yes" : "No");
  earbrain::logging::infof(TAG, "STA connected: %s", status.sta_connected ? "Yes" : "No");

  earbrain::logging::info("", TAG);
  earbrain::logging::info("Performing WiFi scan...", TAG);
  auto scan_result = earbrain::wifi().perform_scan();

  if (scan_result.error == ESP_OK) {
    earbrain::logging::infof(TAG, "Found %zu networks:", scan_result.networks.size());
    earbrain::logging::info("", TAG);

    for (size_t i = 0; i < scan_result.networks.size() && i < 10; i++) {
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
      earbrain::logging::infof(TAG, "      Signal: %d%% | Ch: %d | Auth: %s",
                               net.signal, net.channel, auth_mode);
    }

    if (scan_result.networks.size() > 10) {
      earbrain::logging::infof(TAG, "... and %zu more networks",
                               scan_result.networks.size() - 10);
    }
  } else {
    earbrain::logging::errorf(TAG, "Scan failed: %s", esp_err_to_name(scan_result.error));
  }

  earbrain::logging::info("", TAG);
  earbrain::logging::info("Demo completed. Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
