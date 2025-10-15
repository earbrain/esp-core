#pragma once

#include "completion.hpp"
#include "esp_err.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "esp_wifi_types.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct esp_netif_obj;

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

struct WifiCredentials {
  std::string ssid;
  std::string passphrase;
};

struct AccessPointConfig {
  std::string ssid = "core-ap";
  uint8_t channel = 1;
  wifi_auth_mode_t auth_mode = WIFI_AUTH_OPEN;
  uint8_t max_connections = 4;
};

struct ProvisioningOptions {
  std::string ap_ssid = "esp-provisioning";
  uint8_t ap_channel = 1;
  wifi_auth_mode_t ap_auth_mode = WIFI_AUTH_OPEN;
  uint8_t ap_max_connections = 4;
  uint32_t timeout_ms = 120000;
};

struct WifiConfig {
  AccessPointConfig ap_config;
};

enum class WifiMode {
  Off,
  STA,
  AP,
  APSTA
};

enum class ProvisionMode {
  SmartConfig,
  SoftAP
};

enum class WifiEvent {
  Connected,
  Disconnected,
  ConnectionFailed,
  ProvisioningCredentialsReceived,
  ProvisioningCompleted,
  ProvisioningFailed,
  StateChanged
};

struct WifiEventData {
  WifiEvent event;
  WifiMode mode = WifiMode::Off;
  bool sta_connected = false;
  bool sta_connecting = false;
  bool provisioning_active = false;
  esp_err_t error_code = ESP_OK;
  std::optional<esp_ip4_addr_t> ip_address;
  std::optional<wifi_err_reason_t> disconnect_reason;
  std::optional<WifiCredentials> credentials;
};

struct WifiStatus {
  WifiMode mode = WifiMode::Off;
  bool sta_connected = false;
  bool sta_connecting = false;
  bool provisioning_active = false;
  esp_ip4_addr_t sta_ip{};
  wifi_err_reason_t sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
  esp_err_t sta_last_error = ESP_OK;
};

class WifiService {
public:
  using EventListener = std::function<void(const WifiEventData &)>;

  WifiService();
  ~WifiService() = default;

  WifiService(const WifiService &) = delete;
  WifiService &operator=(const WifiService &) = delete;
  WifiService(WifiService &&) = delete;
  WifiService &operator=(WifiService &&) = delete;

  esp_err_t mode(WifiMode new_mode);

  // Configuration management
  WifiConfig config() const;
  esp_err_t config(const WifiConfig &config);

  esp_err_t connect(const WifiCredentials &creds);
  esp_err_t connect();
  esp_err_t connect_sync(const WifiCredentials &creds, uint32_t timeout_ms = 15000);
  esp_err_t connect_sync(uint32_t timeout_ms = 15000);
  esp_err_t save_credentials(std::string_view ssid, std::string_view passphrase);
  std::optional<WifiCredentials> load_credentials();

  esp_err_t start_provisioning(ProvisionMode mode, const ProvisioningOptions &opts = {});
  esp_err_t cancel_provisioning();

  WifiScanResult perform_scan() const;
  WifiStatus status() const;

  WifiMode mode() const { return current_mode; }
  void on(EventListener listener);

private:
  static wifi_mode_t to_native_mode(WifiMode mode);
  esp_err_t ensure_initialized();
  esp_err_t register_event_handlers();
  esp_err_t start_wifi_sta_mode();
  void reset_sta_state();
  static void ip_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
  static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data);
  static void provisioning_event_handler(void *arg, esp_event_base_t event_base,
                                         int32_t event_id, void *event_data);
  void on_sta_got_ip(const ip_event_got_ip_t &event);
  void on_sta_disconnected(const wifi_event_sta_disconnected_t &event);
  void on_provisioning_done(void *event_data);

  void emit(const WifiEventData &data) const;
  void emit_connection_failed(esp_err_t error);

  esp_netif_obj *softap_netif;
  esp_netif_obj *sta_netif;
  WifiConfig wifi_config;
  WifiCredentials credentials;
  std::optional<WifiCredentials> cached_credentials;
  std::optional<WifiCredentials> temp_provisioning_credentials;
  bool initialized;
  bool handlers_registered;
  std::atomic<bool> sta_connected;
  std::atomic<bool> sta_connecting;
  std::atomic<bool> sta_manual_disconnect;
  std::atomic<esp_ip4_addr_t> sta_ip;
  std::atomic<wifi_err_reason_t> sta_last_disconnect_reason;
  std::atomic<esp_err_t> sta_last_error;
  std::atomic<bool> provisioning_active;

  WifiMode current_mode;
  ProvisionMode current_provisioning_mode;
  std::vector<EventListener> listeners;
  std::shared_ptr<Completion<esp_err_t>> sync_completion;
};

WifiService &wifi();

// Helper functions to convert enums and IP to string
std::string ip_to_string(const esp_ip4_addr_t &ip);
const char* wifi_event_to_string(WifiEvent event);
const char* wifi_mode_to_string(WifiMode mode);

} // namespace earbrain
