#include "earbrain/wifi_service.hpp"
#include "earbrain/logging.hpp"
#include "earbrain/validation.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_smartconfig.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace earbrain {

namespace {

constexpr const char wifi_tag[] = "wifi";

constexpr uint8_t sta_listen_interval = 1;
constexpr int8_t sta_tx_power_qdbm = 78;
constexpr int sta_max_connect_retries = 5;
constexpr uint32_t sta_connection_timeout_ms = 15000;
constexpr uint32_t sta_connection_check_interval_ms = 500;

int signal_quality_from_rssi(int32_t rssi) {
  if (rssi <= -100) {
    return 0;
  }
  if (rssi >= -50) {
    return 100;
  }
  const int quality = 2 * (static_cast<int>(rssi) + 100);
  return std::clamp(quality, 0, 100);
}

std::string format_bssid(const uint8_t (&bssid)[6]) {
  char buffer[18] = {0};
  std::snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  return std::string(buffer);
}

wifi_config_t make_ap_config(const AccessPointConfig &config) {
  wifi_config_t cfg{};
  std::fill(std::begin(cfg.ap.ssid), std::end(cfg.ap.ssid), '\0');
  std::copy_n(config.ssid.data(), config.ssid.size(), cfg.ap.ssid);
  cfg.ap.ssid_len = config.ssid.size();
  cfg.ap.channel = config.channel;
  cfg.ap.authmode = config.auth_mode;
  cfg.ap.max_connection = config.max_connections;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  cfg.ap.pmf_cfg.capable = true;
  cfg.ap.pmf_cfg.required = false;
#endif
  return cfg;
}

wifi_config_t make_sta_config(const StationConfig &config) {
  wifi_config_t cfg{};
  std::fill(std::begin(cfg.sta.ssid), std::end(cfg.sta.ssid), '\0');
  std::copy_n(config.ssid.data(), config.ssid.size(), cfg.sta.ssid);

  std::fill(std::begin(cfg.sta.password), std::end(cfg.sta.password), '\0');
  if (!config.passphrase.empty()) {
    std::copy_n(config.passphrase.data(), config.passphrase.size(),
                cfg.sta.password);
  }

  cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  cfg.sta.listen_interval = sta_listen_interval;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  cfg.sta.pmf_cfg.capable = true;
  cfg.sta.pmf_cfg.required = false;
#endif
  cfg.sta.threshold.authmode =
      config.passphrase.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  return cfg;
}

} // namespace

WifiService::WifiService()
    : softap_netif(nullptr), sta_netif(nullptr), ap_config{}, sta_config{},
      initialized(false), handlers_registered(false), sta_connected(false),
      sta_retry_count(0), sta_manual_disconnect(false), sta_ip{},
      sta_last_disconnect_reason(WIFI_REASON_UNSPECIFIED),
      sta_last_error(ESP_OK), smartconfig_active(false),
      smartconfig_done(false) {}

esp_err_t WifiService::ensure_initialized() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    err = nvs_flash_erase();
    if (err != ESP_OK) {
      return err;
    }
    err = nvs_flash_init();
  }
  if (err != ESP_OK && err != ESP_ERR_NVS_INVALID_STATE) {
    return err;
  }

  err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }

  if (!softap_netif) {
    softap_netif = esp_netif_create_default_wifi_ap();
    if (!softap_netif) {
      return ESP_FAIL;
    }
  }

  if (!sta_netif) {
    sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif) {
      return ESP_FAIL;
    }
  }

  if (!initialized) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
      return err;
    }
    initialized = true;
  }

  err = register_event_handlers();
  if (err != ESP_OK) {
    return err;
  }

  return ESP_OK;
}

esp_err_t WifiService::register_event_handlers() {
  if (handlers_registered) {
    return ESP_OK;
  }

  esp_err_t err = esp_event_handler_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiService::ip_event_handler, this);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }

  err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                   &WifiService::wifi_event_handler, this);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                 &WifiService::ip_event_handler);
    return err;
  }

  handlers_registered = true;
  return ESP_OK;
}

esp_err_t WifiService::start_wifi_sta_mode() {
  esp_err_t err = ensure_initialized();
  if (err != ESP_OK) {
    return err;
  }

  // Stop WiFi if already running
  esp_err_t stop_err = esp_wifi_stop();
  if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED &&
      stop_err != ESP_ERR_WIFI_NOT_INIT) {
    logging::warnf(wifi_tag,
                   "Failed to stop Wi-Fi before starting STA mode: %s",
                   esp_err_to_name(stop_err));
    return stop_err;
  }

  // Set to STA mode
  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to set WiFi mode to STA: %s",
                    esp_err_to_name(err));
    return err;
  }

  // Start WiFi
  err = esp_wifi_start();
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to start WiFi in STA mode: %s",
                    esp_err_to_name(err));
    esp_wifi_set_mode(WIFI_MODE_NULL);
    return err;
  }

  reset_sta_state();
  return ESP_OK;
}

void WifiService::reset_sta_state() {
  sta_connected = false;
  sta_retry_count = 0;
  sta_last_error = ESP_OK;
  sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
  sta_ip.store({.addr = 0});
}

esp_err_t WifiService::start_access_point(const AccessPointConfig &config) {
  if (!validation::is_valid_ssid(config.ssid)) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = ensure_initialized();
  if (err != ESP_OK) {
    return err;
  }

  esp_err_t stop_err = esp_wifi_stop();
  if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED &&
      stop_err != ESP_ERR_WIFI_NOT_INIT) {
    logging::warnf(wifi_tag, "Failed to stop Wi-Fi before starting AP: %s",
                   esp_err_to_name(stop_err));
    return stop_err;
  }

  wifi_config_t ap_cfg = make_ap_config(config);

  err = esp_wifi_set_mode(WIFI_MODE_APSTA);
  if (err != ESP_OK) {
    return err;
  }

  err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
  if (err != ESP_OK) {
    logging::warnf(wifi_tag, "Failed to configure AP interface: %s",
                   esp_err_to_name(err));
    esp_wifi_set_mode(WIFI_MODE_NULL);
    return err;
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    esp_wifi_set_mode(WIFI_MODE_NULL);
    return err;
  }

  ap_config = config;
  sta_connected = false;
  sta_retry_count = 0;
  sta_last_error = ESP_OK;
  sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
  sta_ip.store({.addr = 0});
  logging::infof(wifi_tag, "Access point started (APSTA mode): %s",
                 ap_config.ssid.c_str());
  return ESP_OK;
}

esp_err_t WifiService::start_access_point() {
  return start_access_point(ap_config);
}

esp_err_t WifiService::stop_access_point() {
  esp_err_t err = esp_wifi_stop();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED &&
      err != ESP_ERR_WIFI_NOT_INIT) {
    return err;
  }

  esp_wifi_set_mode(WIFI_MODE_NULL);
  logging::info("Access point stopped", wifi_tag);
  return ESP_OK;
}

esp_err_t WifiService::start_station() {
  esp_err_t err = start_wifi_sta_mode();
  if (err != ESP_OK) {
    return err;
  }

  logging::info("Station started", wifi_tag);

  // Load saved credentials and connect
  auto saved_config = load_credentials();
  if (!saved_config.has_value()) {
    logging::warn("No saved credentials found, station started without connection", wifi_tag);
    return ESP_OK;
  }

  logging::infof(wifi_tag, "Connecting to saved WiFi: %s", saved_config->ssid.c_str());
  return try_connect(saved_config.value());
}

esp_err_t WifiService::start_station(const StationConfig &config) {
  esp_err_t err = start_wifi_sta_mode();
  if (err != ESP_OK) {
    return err;
  }

  logging::infof(wifi_tag, "Station started, connecting to: %s", config.ssid.c_str());

  // Save credentials and connect
  err = save_credentials(config.ssid, config.passphrase);
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to save credentials: %s", esp_err_to_name(err));
    return err;
  }

  return try_connect(config);
}

esp_err_t WifiService::stop_station() {
  esp_err_t err = esp_wifi_stop();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED &&
      err != ESP_ERR_WIFI_NOT_INIT) {
    logging::errorf(wifi_tag, "Failed to stop WiFi: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_wifi_set_mode(WIFI_MODE_NULL);
  if (err != ESP_OK) {
    logging::warnf(wifi_tag, "Failed to set WiFi mode to NULL: %s",
                   esp_err_to_name(err));
  }

  sta_connected = false;
  sta_retry_count = 0;
  sta_last_error = ESP_OK;
  sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
  sta_ip.store({.addr = 0});
  logging::info("Station stopped", wifi_tag);
  return ESP_OK;
}

esp_err_t WifiService::try_connect(const StationConfig &config) {
  esp_err_t err = ensure_initialized();
  if (err != ESP_OK) {
    sta_last_error = err;
    return err;
  }

  // Check current WiFi mode
  wifi_mode_t current_mode = WIFI_MODE_NULL;
  err = esp_wifi_get_mode(&current_mode);
  if (err != ESP_OK) {
    sta_last_error = err;
    return err;
  }

  // STA or APSTA mode required (AP-only won't work)
  if (current_mode != WIFI_MODE_STA && current_mode != WIFI_MODE_APSTA) {
    logging::warn("try_connect() requires STA or APSTA mode", wifi_tag);
    sta_last_error = ESP_ERR_INVALID_STATE;
    return ESP_ERR_INVALID_STATE;
  }

  // Disconnect if already connected
  sta_manual_disconnect.store(true);
  esp_err_t disconnect_err = esp_wifi_disconnect();
  if (disconnect_err != ESP_OK) {
    sta_manual_disconnect.store(false);
    if (disconnect_err != ESP_ERR_WIFI_NOT_STARTED &&
        disconnect_err != ESP_ERR_WIFI_NOT_INIT &&
        disconnect_err != ESP_ERR_WIFI_NOT_CONNECT) {
      logging::warnf(wifi_tag, "Failed to disconnect before reconnecting: %s",
                     esp_err_to_name(disconnect_err));
    }
  }

  // Configure STA interface
  wifi_config_t sta_cfg = make_sta_config(config);
  err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  if (err != ESP_OK) {
    sta_last_error = err;
    logging::errorf(wifi_tag, "Failed to configure STA interface: %s",
                    esp_err_to_name(err));
    return err;
  }

  // Attempt to connect
  err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
    sta_last_error = err;
    logging::errorf(wifi_tag, "Failed to initiate connection: %s",
                    esp_err_to_name(err));
    return err;
  }

  sta_config = config;
  sta_connected = false;
  sta_retry_count = 0;
  sta_last_error = ESP_OK;
  sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
  sta_ip.store({.addr = 0});
  logging::infof(wifi_tag,
                 "Station connection initiated: ssid='%s', passphrase_len=%zu",
                 sta_config.ssid.c_str(), sta_config.passphrase.size());

  // Wait for connection result
  constexpr uint32_t timeout_ms = 15000;
  constexpr uint32_t check_interval_ms = 500;
  uint32_t elapsed_ms = 0;

  while (elapsed_ms < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
    elapsed_ms += check_interval_ms;

    // Connection successful
    if (sta_connected) {
      logging::infof(wifi_tag, "Successfully connected to SSID: %s",
                     sta_config.ssid.c_str());
      return ESP_OK;
    }

    // Connection failed with error
    if (sta_last_error != ESP_OK) {
      logging::errorf(wifi_tag, "Connection failed: %s (disconnect_reason=%d)",
                      esp_err_to_name(sta_last_error),
                      static_cast<int>(sta_last_disconnect_reason));
      return sta_last_error;
    }

    // Disconnected with a reason (e.g., wrong password)
    if (sta_last_disconnect_reason != WIFI_REASON_UNSPECIFIED) {
      logging::errorf(wifi_tag, "Connection failed (disconnect_reason=%d)",
                      static_cast<int>(sta_last_disconnect_reason));

      // Map disconnect reason to error code
      switch (sta_last_disconnect_reason) {
      case WIFI_REASON_AUTH_FAIL:
        sta_last_error = ESP_ERR_WIFI_PASSWORD;
        break;
      case WIFI_REASON_AUTH_EXPIRE:
      case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      case WIFI_REASON_HANDSHAKE_TIMEOUT:
        sta_last_error = ESP_ERR_TIMEOUT;
        break;
      case WIFI_REASON_NO_AP_FOUND:
        sta_last_error = ESP_ERR_WIFI_SSID;
        break;
      default:
        sta_last_error = ESP_FAIL;
        break;
      }
      return sta_last_error;
    }
  }

  // Timeout
  logging::warn("Connection attempt timed out", wifi_tag);
  sta_last_error = ESP_ERR_TIMEOUT;
  return ESP_ERR_TIMEOUT;
}

esp_err_t WifiService::save_credentials(std::string_view ssid,
                                        std::string_view passphrase) {
  wifi_config_t wifi_config = {};

  // Copy SSID (max 32 bytes)
  size_t ssid_len = std::min(ssid.length(), sizeof(wifi_config.sta.ssid) - 1);
  std::memcpy(wifi_config.sta.ssid, ssid.data(), ssid_len);
  wifi_config.sta.ssid[ssid_len] = '\0';

  // Copy passphrase (max 64 bytes)
  size_t pass_len =
      std::min(passphrase.length(), sizeof(wifi_config.sta.password) - 1);
  std::memcpy(wifi_config.sta.password, passphrase.data(), pass_len);
  wifi_config.sta.password[pass_len] = '\0';

  // Set the WiFi configuration which will be stored in NVS
  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

  if (err == ESP_OK) {
    // Cache the credentials
    credentials = StationConfig{.ssid = std::string(ssid),

                                .passphrase = std::string(passphrase)};
    logging::infof(wifi_tag, "Saved Wi-Fi credentials for SSID: %s",
                   std::string(ssid).c_str());
  } else {
    logging::errorf(wifi_tag, "Failed to save Wi-Fi credentials: %s",
                    esp_err_to_name(err));
  }

  return err;
}

std::optional<StationConfig> WifiService::load_credentials() {
  if (credentials.has_value()) {
    return credentials;
  }

  // Load from NVS
  wifi_config_t wifi_config = {};
  esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to load Wi-Fi credentials: %s",
                    esp_err_to_name(err));
    return std::nullopt;
  }

  // Check if SSID is empty
  if (wifi_config.sta.ssid[0] == '\0') {
    logging::info("No saved Wi-Fi credentials found", wifi_tag);
    return std::nullopt;
  }

  // Convert to StationConfig and cache it
  StationConfig config;
  config.ssid =
      std::string(reinterpret_cast<const char *>(wifi_config.sta.ssid));
  config.passphrase =
      std::string(reinterpret_cast<const char *>(wifi_config.sta.password));

  credentials = config;
  logging::infof(wifi_tag, "Loaded saved Wi-Fi credentials for SSID: %s",
                 config.ssid.c_str());

  return config;
}

esp_err_t WifiService::try_connect() {
  esp_err_t err = ensure_initialized();
  if (err != ESP_OK) {
    sta_last_error = err;
    return err;
  }

  auto saved_config = load_credentials();
  if (!saved_config.has_value()) {
    sta_last_error = ESP_ERR_NOT_FOUND;
    logging::warn("No saved credentials found", wifi_tag);
    return ESP_ERR_NOT_FOUND;
  }

  return try_connect(saved_config.value());
}

void WifiService::ip_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data) {
  if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP ||
      !event_data) {
    return;
  }
  auto *wifi = static_cast<WifiService *>(arg);
  if (!wifi) {
    return;
  }
  const auto *event = static_cast<ip_event_got_ip_t *>(event_data);
  wifi->on_sta_got_ip(*event);
}

void WifiService::wifi_event_handler(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data) {
  if (event_base != WIFI_EVENT) {
    return;
  }
  auto *wifi = static_cast<WifiService *>(arg);
  if (!wifi) {
    return;
  }

  switch (event_id) {
  case WIFI_EVENT_STA_DISCONNECTED:
    if (event_data) {
      const auto *event =
          static_cast<wifi_event_sta_disconnected_t *>(event_data);
      wifi->on_sta_disconnected(*event);
    }
    break;
  default:
    break;
  }
}

void WifiService::on_sta_got_ip(const ip_event_got_ip_t &event) {
  sta_connected = true;
  sta_retry_count = 0;
  sta_last_error = ESP_OK;
  sta_ip.store(event.ip_info.ip);
  sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;

  esp_ip4_addr_t ip = sta_ip.load();
  const ip4_addr_t *ip4 = reinterpret_cast<const ip4_addr_t *>(&ip);
  char ip_buffer[16] = {0};
  ip4addr_ntoa_r(ip4, ip_buffer, sizeof(ip_buffer));
  logging::infof(wifi_tag, "Station got IP: %s", ip_buffer);

  // Auto-save SmartConfig credentials after successful connection
  if (smartconfig_active.load() && temp_smartconfig_credentials.has_value()) {
    esp_err_t err = save_credentials(temp_smartconfig_credentials->ssid,
                                     temp_smartconfig_credentials->passphrase);

    if (err == ESP_OK) {
      logging::info("SmartConfig credentials verified and saved successfully",
                    wifi_tag);
      smartconfig_done.store(true);
    } else {
      logging::errorf(wifi_tag, "Failed to save SmartConfig credentials: %s",
                      esp_err_to_name(err));
    }

    temp_smartconfig_credentials.reset();
  }
}

void WifiService::on_sta_disconnected(
    const wifi_event_sta_disconnected_t &event) {
  sta_connected = false;
  sta_ip.store({.addr = 0});
  const bool manual = sta_manual_disconnect.exchange(false);

  if (manual && event.reason == WIFI_REASON_ASSOC_LEAVE) {
    sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
    sta_last_error = ESP_OK;
    logging::info("Station disconnected intentionally (manual reconnect)",
                  wifi_tag);
    return;
  }

  sta_last_disconnect_reason = static_cast<wifi_err_reason_t>(event.reason);
  logging::warnf(wifi_tag, "Station disconnected (reason=%d)",
                 static_cast<int>(event.reason));
}

WifiScanResult WifiService::perform_scan() {
  WifiScanResult result{};

  // WiFi must be started before scanning
  wifi_mode_t current_mode = WIFI_MODE_NULL;
  esp_err_t err = esp_wifi_get_mode(&current_mode);
  if (err != ESP_OK || current_mode == WIFI_MODE_NULL) {
    result.error = ESP_ERR_INVALID_STATE;
    logging::warn("Cannot scan: WiFi not started", wifi_tag);
    return result;
  }

  // Start scan
  wifi_scan_config_t scan_cfg{};
  scan_cfg.show_hidden = true;

  err = esp_wifi_scan_start(&scan_cfg, true);
  if (err != ESP_OK) {
    result.error = err;
    return result;
  }

  // Get scan results
  uint16_t ap_count = 0;
  err = esp_wifi_scan_get_ap_num(&ap_count);
  std::vector<wifi_ap_record_t> records;
  if (err == ESP_OK && ap_count > 0) {
    records.resize(ap_count);
    err = esp_wifi_scan_get_ap_records(&ap_count, records.data());
    if (err == ESP_OK) {
      records.resize(ap_count);
    } else {
      records.clear();
    }
  }

  if (err != ESP_OK) {
    result.error = err;
    return result;
  }

  // Build network list
  result.networks.reserve(records.size());

  for (const auto &record : records) {
    const char *ssid_raw = reinterpret_cast<const char *>(record.ssid);
    if (!ssid_raw || ssid_raw[0] == '\0') {
      continue;
    }

    WifiNetworkSummary summary{};
    summary.ssid = ssid_raw;
    summary.bssid = format_bssid(record.bssid);
    summary.rssi = record.rssi;
    summary.signal = signal_quality_from_rssi(record.rssi);
    summary.channel = record.primary;
    summary.auth_mode = record.authmode;
    summary.hidden = record.ssid[0] == '\0';

    // Only check for connected network if actually connected
    summary.connected = false;
    if (sta_connected && !sta_config.ssid.empty()) {
      summary.connected = (sta_config.ssid == summary.ssid);
    }

    result.networks.push_back(std::move(summary));
  }

  // Sort by signal strength
  std::sort(result.networks.begin(), result.networks.end(),
            [](const WifiNetworkSummary &a, const WifiNetworkSummary &b) {
              return a.signal > b.signal;
            });

  result.error = ESP_OK;
  return result;
}

WifiStatus WifiService::status() const {
  WifiStatus s;
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK) {
    mode = WIFI_MODE_NULL;
  }
  s.ap_active = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
  s.sta_active = (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA);

  if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
    s.sta_connected = sta_connected.load();
    s.sta_ip = sta_ip.load();
    s.sta_last_disconnect_reason = sta_last_disconnect_reason.load();
    s.sta_last_error = sta_last_error.load();
  } else {
    s.sta_connected = false;
    s.sta_ip.addr = 0;
    s.sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
    s.sta_last_error = ESP_OK;
  }

  s.smartconfig_active = smartconfig_active.load();

  return s;
}

esp_err_t WifiService::start_smart_config() {
  if (smartconfig_active.load()) {
    logging::warn("SmartConfig is already active", wifi_tag);
    return ESP_ERR_INVALID_STATE;
  }

  // Start WiFi in STA mode (without connecting)
  esp_err_t err = start_wifi_sta_mode();
  if (err != ESP_OK) {
    return err;
  }

  logging::info("WiFi started for SmartConfig", wifi_tag);

  // Register SmartConfig event handlers
  err =
      esp_event_handler_register(SC_EVENT, SC_EVENT_GOT_SSID_PSWD,
                                 &WifiService::smartconfig_event_handler, this);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    logging::errorf(wifi_tag,
                    "Failed to register SmartConfig event handler: %s",
                    esp_err_to_name(err));
    return err;
  }

  err =
      esp_event_handler_register(SC_EVENT, SC_EVENT_SEND_ACK_DONE,
                                 &WifiService::smartconfig_event_handler, this);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    esp_event_handler_unregister(SC_EVENT, SC_EVENT_GOT_SSID_PSWD,
                                 &WifiService::smartconfig_event_handler);
    logging::errorf(wifi_tag, "Failed to register SmartConfig ACK handler: %s",
                    esp_err_to_name(err));
    return err;
  }

  // Start SmartConfig with ESP-TOUCH V2 protocol
  smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
  err = esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2);
  if (err != ESP_OK) {
    logging::warnf(wifi_tag, "Failed to set SmartConfig type: %s",
                   esp_err_to_name(err));
  }

  err = esp_smartconfig_start(&cfg);
  if (err != ESP_OK) {
    esp_event_handler_unregister(SC_EVENT, SC_EVENT_GOT_SSID_PSWD,
                                 &WifiService::smartconfig_event_handler);
    esp_event_handler_unregister(SC_EVENT, SC_EVENT_SEND_ACK_DONE,
                                 &WifiService::smartconfig_event_handler);
    logging::errorf(wifi_tag, "Failed to start SmartConfig: %s",
                    esp_err_to_name(err));
    return err;
  }

  smartconfig_active.store(true);
  smartconfig_done.store(false);
  logging::info("SmartConfig started", wifi_tag);
  return ESP_OK;
}

esp_err_t WifiService::stop_smart_config() {
  if (!smartconfig_active.load()) {
    return ESP_OK;
  }

  esp_err_t err = esp_smartconfig_stop();
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to stop SmartConfig: %s",
                    esp_err_to_name(err));
    return err;
  }

  // Unregister event handlers
  esp_event_handler_unregister(SC_EVENT, SC_EVENT_GOT_SSID_PSWD,
                               &WifiService::smartconfig_event_handler);
  esp_event_handler_unregister(SC_EVENT, SC_EVENT_SEND_ACK_DONE,
                               &WifiService::smartconfig_event_handler);

  smartconfig_active.store(false);
  smartconfig_done.store(false);
  logging::info("SmartConfig stopped", wifi_tag);
  return ESP_OK;
}

esp_err_t WifiService::wait_for_smart_config(uint32_t timeout_ms) {
  if (!smartconfig_active.load()) {
    return ESP_ERR_INVALID_STATE;
  }

  constexpr uint32_t check_interval_ms = 500;
  uint32_t elapsed_ms = 0;

  while (elapsed_ms < timeout_ms) {
    if (smartconfig_done.load()) {
      logging::info("SmartConfig completed successfully", wifi_tag);
      return ESP_OK;
    }

    vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
    elapsed_ms += check_interval_ms;

    // Check if SmartConfig was stopped externally
    if (!smartconfig_active.load()) {
      logging::warn("SmartConfig was stopped externally", wifi_tag);
      return ESP_ERR_INVALID_STATE;
    }
  }

  logging::warn("SmartConfig timed out", wifi_tag);
  return ESP_ERR_TIMEOUT;
}

bool WifiService::is_smart_config_active() const {
  return smartconfig_active.load();
}

void WifiService::smartconfig_event_handler(void *arg,
                                            esp_event_base_t event_base,
                                            int32_t event_id,
                                            void *event_data) {
  if (event_base != SC_EVENT) {
    return;
  }

  auto *wifi = static_cast<WifiService *>(arg);
  if (!wifi) {
    return;
  }

  switch (event_id) {
  case SC_EVENT_GOT_SSID_PSWD:
    if (event_data) {
      wifi->on_smartconfig_done(event_data);
    }
    break;
  case SC_EVENT_SEND_ACK_DONE:
    logging::info("SmartConfig ACK sent to phone", wifi_tag);
    break;
  default:
    break;
  }
}

void WifiService::on_smartconfig_done(void *event_data) {
  if (!event_data) {
    return;
  }

  const auto *event =
      static_cast<smartconfig_event_got_ssid_pswd_t *>(event_data);
  std::string ssid(reinterpret_cast<const char *>(event->ssid),
                   strnlen(reinterpret_cast<const char *>(event->ssid),
                           sizeof(event->ssid)));
  std::string passphrase(
      reinterpret_cast<const char *>(event->password),
      strnlen(reinterpret_cast<const char *>(event->password),
              sizeof(event->password)));

  logging::infof(
      wifi_tag,
      "SmartConfig received credentials: SSID='%s', passphrase_len=%zu",
      ssid.c_str(), passphrase.size());

  // Store credentials temporarily (will be saved on successful connection)
  temp_smartconfig_credentials = StationConfig{ssid, passphrase};

  // Configure STA interface with the received credentials
  wifi_config_t sta_cfg = make_sta_config(*temp_smartconfig_credentials);
  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to configure STA interface: %s",
                    esp_err_to_name(err));
    temp_smartconfig_credentials.reset();
    return;
  }

  // Attempt to connect (non-blocking)
  err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
    logging::errorf(wifi_tag, "Failed to initiate connection: %s",
                    esp_err_to_name(err));
    temp_smartconfig_credentials.reset();
    return;
  }

  logging::info("SmartConfig: Connection initiated, waiting for IP address...",
                wifi_tag);
}

WifiService &wifi() {
  static WifiService instance;
  return instance;
}

} // namespace earbrain
