#pragma once

#include "earbrain/wifi_scan.hpp"

#include "esp_err.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "esp_wifi_types.h"
#include <atomic>
#include <optional>
#include <string>
#include <string_view>

struct esp_netif_obj;

namespace earbrain {

struct StationConfig {
  std::string ssid;
  std::string passphrase;
};

struct AccessPointConfig {
  std::string ssid = "core-ap";
  uint8_t channel = 1;
  wifi_auth_mode_t auth_mode = WIFI_AUTH_OPEN;
  uint8_t max_connections = 4;
};

struct WifiStatus {
  bool ap_active = false;
  bool sta_active = false;
  bool sta_connected = false;
  bool smartconfig_active = false;
  esp_ip4_addr_t sta_ip{};
  wifi_err_reason_t sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
  esp_err_t sta_last_error = ESP_OK;
};

class WifiService {
public:
  WifiService();
  ~WifiService() = default;

  WifiService(const WifiService&) = delete;
  WifiService& operator=(const WifiService&) = delete;
  WifiService(WifiService&&) = delete;
  WifiService& operator=(WifiService&&) = delete;

  esp_err_t start_station();
  esp_err_t start_station(const StationConfig &config);
  esp_err_t stop_station();

  esp_err_t start_access_point(const AccessPointConfig &config);
  esp_err_t start_access_point();
  esp_err_t stop_access_point();

  esp_err_t try_connect(const StationConfig &config);
  esp_err_t try_connect();

  esp_err_t save_credentials(std::string_view ssid, std::string_view passphrase);
  std::optional<StationConfig> load_credentials();

  // SmartConfig operations
  esp_err_t start_smart_config();
  esp_err_t stop_smart_config();
  esp_err_t wait_for_smart_config(uint32_t timeout_ms = 120000);
  bool is_smart_config_active() const;

  WifiScanResult perform_scan();
  WifiStatus status() const;

private:
  esp_err_t ensure_initialized();
  esp_err_t register_event_handlers();
  esp_err_t start_wifi_sta_mode();
  wifi_mode_t get_wifi_mode();
  void reset_sta_state();
  static void ip_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
  static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data);
  static void smartconfig_event_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data);
  void on_sta_got_ip(const ip_event_got_ip_t &event);
  void on_sta_disconnected(const wifi_event_sta_disconnected_t &event);
  void on_smartconfig_done(void *event_data);

  esp_netif_obj *softap_netif;
  esp_netif_obj *sta_netif;
  AccessPointConfig ap_config;
  StationConfig sta_config;
  std::optional<StationConfig> credentials;
  std::optional<StationConfig> temp_smartconfig_credentials;
  bool initialized;
  bool handlers_registered;
  std::atomic<bool> sta_connected;
  std::atomic<bool> sta_manual_disconnect;
  std::atomic<esp_ip4_addr_t> sta_ip;
  std::atomic<wifi_err_reason_t> sta_last_disconnect_reason;
  std::atomic<esp_err_t> sta_last_error;
  std::atomic<bool> smartconfig_active;
  std::atomic<bool> smartconfig_done;
};

WifiService& wifi();

} // namespace earbrain
