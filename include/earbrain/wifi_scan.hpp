#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace earbrain {

struct WifiNetworkSummary {
  std::string ssid;
  std::string bssid;
  int32_t rssi = -127;
  int signal = 0;
  uint8_t channel = 0;
  wifi_auth_mode_t auth_mode = WIFI_AUTH_OPEN;
  bool connected = false;
  bool hidden = false;
};

struct WifiScanResult {
  std::vector<WifiNetworkSummary> networks;
  esp_err_t error = ESP_OK;
};

} // namespace earbrain
