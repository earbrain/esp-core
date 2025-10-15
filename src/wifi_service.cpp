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
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace earbrain {

namespace {

constexpr char wifi_tag[] = "wifi";

constexpr uint8_t sta_listen_interval = 1;
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

// Safe copy helpers for SSID and password
void copy_ssid_safe(uint8_t (&dst)[32], std::string_view src) {
  const size_t len = std::min(src.size(), size_t(32));
  if (src.size() > 32) {
    logging::warnf(wifi_tag, "SSID truncated from %zu to 32 bytes", src.size());
  }
  std::memset(dst, 0, 32);
  std::memcpy(dst, src.data(), len);
}

void copy_password_safe(uint8_t (&dst)[64], std::string_view src) {
  const size_t len = std::min(src.size(), size_t(64));
  if (src.size() > 64) {
    logging::warnf(wifi_tag, "Password truncated from %zu to 64 bytes", src.size());
  }
  std::memset(dst, 0, 64);
  std::memcpy(dst, src.data(), len);
}

wifi_config_t make_ap_config(const AccessPointConfig &config) {
  wifi_config_t cfg{};
  copy_ssid_safe(cfg.ap.ssid, config.ssid);
  cfg.ap.ssid_len = std::min<uint8_t>(config.ssid.size(), 32);
  cfg.ap.channel = config.channel;
  cfg.ap.authmode = config.auth_mode;
  cfg.ap.max_connection = config.max_connections;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  cfg.ap.pmf_cfg.capable = true;
  cfg.ap.pmf_cfg.required = false;
#endif
  return cfg;
}

wifi_config_t make_sta_config(const WifiCredentials &creds) {
  wifi_config_t cfg{};
  copy_ssid_safe(cfg.sta.ssid, creds.ssid);
  if (!creds.passphrase.empty()) {
    copy_password_safe(cfg.sta.password, creds.passphrase);
  }

  cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  cfg.sta.listen_interval = sta_listen_interval;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  cfg.sta.pmf_cfg.capable = true;
  cfg.sta.pmf_cfg.required = false;
#endif
  cfg.sta.threshold.authmode =
      creds.passphrase.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  return cfg;
}

esp_err_t validate_station_config(const WifiCredentials &creds) {
  if (!validation::is_valid_ssid(creds.ssid)) {
    logging::error("Invalid STA SSID (length must be 1-32 bytes)", wifi_tag);
    return ESP_ERR_INVALID_ARG;
  }
  if (!validation::is_valid_passphrase(creds.passphrase)) {
    logging::error("Invalid STA passphrase (length must be 0 for open networks, 8-63, or 64 hex)", wifi_tag);
    return ESP_ERR_INVALID_ARG;
  }
  return ESP_OK;
}

} // namespace

WifiService::WifiService()
    : softap_netif(nullptr), sta_netif(nullptr), wifi_config{}, credentials{},
      initialized(false), handlers_registered(false), sta_connected(false),
      sta_connecting(false), sta_manual_disconnect(false), sta_ip{},
      sta_last_disconnect_reason(WIFI_REASON_UNSPECIFIED),
      sta_last_error(ESP_OK), provisioning_active(false),
      current_mode(WifiMode::Off),
      current_provisioning_mode(ProvisionMode::SmartConfig) {}

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
  current_mode = WifiMode::STA;
  return ESP_OK;
}

void WifiService::reset_sta_state() {
  sta_connected = false;
  sta_last_error = ESP_OK;
  sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
  sta_ip.store({.addr = 0});
}

wifi_mode_t WifiService::to_native_mode(WifiMode mode) {
  switch (mode) {
  case WifiMode::STA:
    return WIFI_MODE_STA;
  case WifiMode::AP:
    return WIFI_MODE_AP;
  case WifiMode::APSTA:
    return WIFI_MODE_APSTA;
  case WifiMode::Off:
  default:
    return WIFI_MODE_NULL;
  }
}

esp_err_t WifiService::mode(WifiMode new_mode) {
  esp_err_t err = ensure_initialized();
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Initialization failed: %s", esp_err_to_name(err));
    return err;
  }

  // Avoid unnecessary restart if already in the requested mode
  if (current_mode == new_mode && initialized) {
    logging::infof(wifi_tag, "WiFi mode unchanged: %d", static_cast<int>(new_mode));
    return ESP_OK;
  }

  esp_err_t stop_err = esp_wifi_stop();
  if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED &&
      stop_err != ESP_ERR_WIFI_NOT_INIT) {
    logging::warnf(wifi_tag, "Failed to stop WiFi before starting: %s",
                   esp_err_to_name(stop_err));
    return stop_err;
  }

  wifi_mode_t native_mode = to_native_mode(new_mode);
  if (native_mode == WIFI_MODE_NULL) {
    logging::warn("Requested start with WifiMode::Off; stopping WiFi instead", wifi_tag);
    current_mode = WifiMode::Off;
    return ESP_OK;
  }

  err = esp_wifi_set_mode(native_mode);
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to set WiFi mode: %s", esp_err_to_name(err));
    return err;
  }

  if (native_mode == WIFI_MODE_AP || native_mode == WIFI_MODE_APSTA) {
    wifi_config_t ap_cfg = make_ap_config(wifi_config.ap_config);
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
      logging::errorf(wifi_tag, "Failed to configure AP interface: %s",
                      esp_err_to_name(err));
      esp_wifi_set_mode(WIFI_MODE_NULL);
      return err;
    }
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to start WiFi: %s", esp_err_to_name(err));
    esp_wifi_set_mode(WIFI_MODE_NULL);
    return err;
  }

  if (native_mode == WIFI_MODE_APSTA) {
    current_mode = WifiMode::APSTA;
    logging::infof(wifi_tag, "APSTA mode started: %s", wifi_config.ap_config.ssid.c_str());
  } else if (native_mode == WIFI_MODE_AP) {
    current_mode = WifiMode::AP;
    logging::infof(wifi_tag, "AP mode started: %s", wifi_config.ap_config.ssid.c_str());
  } else {
    current_mode = WifiMode::STA;
    logging::info("STA mode started", wifi_tag);

    auto saved_credentials = load_credentials();
    if (saved_credentials.has_value()) {
      logging::infof(wifi_tag, "Auto-connecting to: %s", saved_credentials->ssid.c_str());
      connect(saved_credentials.value());
    }
  }

  return ESP_OK;
}

WifiConfig WifiService::config() const {
  return wifi_config;
}

esp_err_t WifiService::config(const WifiConfig &config) {
  if (!validation::is_valid_ssid(config.ap_config.ssid)) {
    logging::error("Invalid AP SSID", wifi_tag);
    return ESP_ERR_INVALID_ARG;
  }

  wifi_config = config;
  logging::infof(wifi_tag, "AP config updated: %s", wifi_config.ap_config.ssid.c_str());
  logging::info("WiFi config updated", wifi_tag);
  return ESP_OK;
}

esp_err_t WifiService::connect(const WifiCredentials &creds) {
  esp_err_t validation_err = validate_station_config(creds);
  if (validation_err != ESP_OK) {
    emit_connection_failed(validation_err);
    return validation_err;
  }

  esp_err_t err = ensure_initialized();
  if (err != ESP_OK) {
    emit_connection_failed(err);
    return err;
  }

  wifi_mode_t mode = WIFI_MODE_NULL;
  err = esp_wifi_get_mode(&mode);
  if (err != ESP_OK) {
    emit_connection_failed(err);
    return err;
  }

  if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
    logging::warn("connect() requires STA or APSTA mode", wifi_tag);
    emit_connection_failed(ESP_ERR_INVALID_STATE);
    return ESP_ERR_INVALID_STATE;
  }

  sta_connecting.store(true);

  // Only disconnect if currently connected
  bool was_connected = false;
  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    was_connected = true;
  }

  if (was_connected) {
    sta_manual_disconnect.store(true);
    esp_wifi_disconnect();
  }

  wifi_config_t sta_cfg = make_sta_config(creds);
  err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to configure STA interface: %s",
                    esp_err_to_name(err));
    emit_connection_failed(err);
    return err;
  }

  err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
    logging::errorf(wifi_tag, "Failed to initiate connection: %s",
                    esp_err_to_name(err));
    emit_connection_failed(err);
    return err;
  }

  credentials = creds;
  sta_connected = false;
  sta_last_error = ESP_OK;
  sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
  sta_ip.store({.addr = 0});
  logging::infof(wifi_tag,
                 "Connection initiated: ssid='%s', passphrase_len=%zu",
                 credentials.ssid.c_str(), credentials.passphrase.size());
  return ESP_OK;
}

esp_err_t WifiService::save_credentials(std::string_view ssid,
                                        std::string_view passphrase) {
  WifiCredentials creds{std::string(ssid), std::string(passphrase)};
  esp_err_t validation_err = validate_station_config(creds);
  if (validation_err != ESP_OK) {
    return validation_err;
  }

  esp_err_t err = ensure_initialized();
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Cannot save credentials: not initialized: %s",
                    esp_err_to_name(err));
    return err;
  }

  wifi_config_t sta_config = {};

  // Copy SSID (max 32 bytes)
  size_t ssid_len = std::min(ssid.length(), sizeof(sta_config.sta.ssid) - 1);
  std::memcpy(sta_config.sta.ssid, ssid.data(), ssid_len);
  sta_config.sta.ssid[ssid_len] = '\0';

  // Copy passphrase (max 64 bytes)
  size_t pass_len =
      std::min(passphrase.length(), sizeof(sta_config.sta.password) - 1);
  std::memcpy(sta_config.sta.password, passphrase.data(), pass_len);
  sta_config.sta.password[pass_len] = '\0';

  // Set the WiFi configuration which will be stored in NVS
  err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);

  if (err == ESP_OK) {
    // Cache the credentials
    cached_credentials = WifiCredentials{.ssid = std::string(ssid),
                                         .passphrase = std::string(passphrase)};
    logging::infof(wifi_tag, "Saved Wi-Fi credentials for SSID: %s",
                   std::string(ssid).c_str());
  } else {
    logging::errorf(wifi_tag, "Failed to save Wi-Fi credentials: %s",
                    esp_err_to_name(err));
  }

  return err;
}

std::optional<WifiCredentials> WifiService::load_credentials() {
  if (cached_credentials.has_value()) {
    return cached_credentials;
  }

  esp_err_t init_err = ensure_initialized();
  if (init_err != ESP_OK) {
    logging::errorf(wifi_tag, "Cannot load credentials: not initialized: %s",
                    esp_err_to_name(init_err));
    return std::nullopt;
  }

  // Load from NVS
  wifi_config_t sta_config = {};
  esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &sta_config);

  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to load Wi-Fi credentials: %s",
                    esp_err_to_name(err));
    return std::nullopt;
  }

  // Check if SSID is empty
  if (sta_config.sta.ssid[0] == '\0') {
    logging::info("No saved Wi-Fi credentials found", wifi_tag);
    return std::nullopt;
  }

  // Convert to WifiCredentials and cache it
  WifiCredentials loaded;
  loaded.ssid =
      std::string(reinterpret_cast<const char *>(sta_config.sta.ssid));
  loaded.passphrase =
      std::string(reinterpret_cast<const char *>(sta_config.sta.password));

  cached_credentials = loaded;
  logging::infof(wifi_tag, "Loaded saved Wi-Fi credentials for SSID: %s",
                 loaded.ssid.c_str());

  return loaded;
}

esp_err_t WifiService::connect() {
  esp_err_t err = ensure_initialized();
  if (err != ESP_OK) {
    emit_connection_failed(err);
    return err;
  }

  auto saved_credentials = load_credentials();
  if (!saved_credentials.has_value()) {
    logging::warn("No saved credentials found", wifi_tag);
    emit_connection_failed(ESP_ERR_NOT_FOUND);
    return ESP_ERR_NOT_FOUND;
  }

  return connect(saved_credentials.value());
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
  sta_last_error = ESP_OK;
  sta_ip.store(event.ip_info.ip);
  sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;

  if (sta_connecting.load()) {
    sta_connecting.store(false);

    if (provisioning_active.load() && temp_provisioning_credentials.has_value()) {
      esp_err_t err = save_credentials(temp_provisioning_credentials->ssid,
                                       temp_provisioning_credentials->passphrase);

      if (err == ESP_OK) {
        WifiEventData event_data{};
        event_data.mode = current_mode;
        event_data.sta_connected = sta_connected.load();
        event_data.sta_connecting = sta_connecting.load();
        event_data.provisioning_active = provisioning_active.load();
        event_data.event = WifiEvent::ProvisioningCompleted;
        event_data.credentials = temp_provisioning_credentials;
        event_data.ip_address = event.ip_info.ip;
        emit(event_data);

        logging::info("Provisioning credentials verified and saved successfully", wifi_tag);

        // For SmartConfig, do NOT stop smartconfig here. Wait for SC_EVENT_SEND_ACK_DONE
        // to ensure the phone receives the success ACK. Cleanup will be handled in the
        // SC_EVENT_SEND_ACK_DONE event handler.
      } else {
        logging::errorf(wifi_tag, "Failed to save provisioning credentials: %s",
                        esp_err_to_name(err));
      }

      temp_provisioning_credentials.reset();
    }
  }

  WifiEventData event_data{};
  event_data.mode = current_mode;
  event_data.sta_connected = sta_connected.load();
  event_data.sta_connecting = sta_connecting.load();
  event_data.provisioning_active = provisioning_active.load();
  event_data.event = WifiEvent::Connected;
  event_data.ip_address = event.ip_info.ip;
  emit(event_data);

  esp_ip4_addr_t ip = sta_ip.load();
  logging::infof(wifi_tag, "Station got IP: %s", ip_to_string(ip).c_str());
}

void WifiService::on_sta_disconnected(
    const wifi_event_sta_disconnected_t &event) {
  sta_connected = false;
  sta_ip.store({.addr = 0});
  const bool manual = sta_manual_disconnect.exchange(false);

  if (manual && event.reason == WIFI_REASON_ASSOC_LEAVE) {
    sta_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
    sta_last_error = ESP_OK;
    logging::info("Station disconnected intentionally (manual reconnect)", wifi_tag);
    return;
  }

  sta_last_disconnect_reason = static_cast<wifi_err_reason_t>(event.reason);

  if (sta_connecting.load()) {
    sta_connecting.store(false);
    esp_err_t error = ESP_FAIL;
    switch (sta_last_disconnect_reason) {
    case WIFI_REASON_AUTH_FAIL:
      error = ESP_ERR_WIFI_PASSWORD;
      break;
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      error = ESP_ERR_TIMEOUT;
      break;
    case WIFI_REASON_NO_AP_FOUND:
      error = ESP_ERR_WIFI_SSID;
      break;
    default:
      error = ESP_FAIL;
      break;
    }
    emit_connection_failed(error);
  }

  WifiEventData event_data{};
  event_data.mode = current_mode;
  event_data.sta_connected = sta_connected.load();
  event_data.sta_connecting = sta_connecting.load();
  event_data.provisioning_active = provisioning_active.load();
  event_data.event = WifiEvent::Disconnected;
  event_data.disconnect_reason = static_cast<wifi_err_reason_t>(event.reason);
  emit(event_data);

  logging::warnf(wifi_tag, "Station disconnected (reason=%d)",
                 static_cast<int>(event.reason));
}

WifiScanResult WifiService::perform_scan() const {
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
    WifiNetworkSummary summary{};

    // Check for hidden network first
    const char *ssid_raw = reinterpret_cast<const char *>(record.ssid);
    size_t ssid_len = ssid_raw ? strnlen(ssid_raw, sizeof(record.ssid)) : 0;
    summary.hidden = (ssid_len == 0);

    // Skip hidden networks (no SSID to display)
    if (summary.hidden) {
      continue;
    }

    summary.ssid.assign(ssid_raw, ssid_len);
    summary.bssid = format_bssid(record.bssid);
    summary.rssi = record.rssi;
    summary.signal = signal_quality_from_rssi(record.rssi);
    summary.channel = record.primary;
    summary.auth_mode = record.authmode;

    // Only check for connected network if actually connected
    summary.connected = false;
    if (sta_connected && !credentials.ssid.empty()) {
      summary.connected = (credentials.ssid == summary.ssid);
    }

    result.networks.push_back(std::move(summary));
  }

  // Sort by signal strength
  std::ranges::sort(result.networks, [](const WifiNetworkSummary &a, const WifiNetworkSummary &b) {
    return a.signal > b.signal;
  });

  result.error = ESP_OK;
  return result;
}

WifiStatus WifiService::status() const {
  WifiStatus s;
  s.mode = current_mode;
  s.sta_connected = sta_connected.load();
  s.sta_connecting = sta_connecting.load();
  s.provisioning_active = provisioning_active.load();
  s.sta_ip = sta_ip.load();
  s.sta_last_disconnect_reason = sta_last_disconnect_reason.load();
  s.sta_last_error = sta_last_error.load();
  return s;
}

esp_err_t WifiService::start_provisioning(ProvisionMode mode, const ProvisioningOptions &opts) {
  if (provisioning_active.load()) {
    logging::warn("Provisioning is already active", wifi_tag);
    return ESP_ERR_INVALID_STATE;
  }

  current_provisioning_mode = mode;

  if (mode == ProvisionMode::SmartConfig) {
    esp_err_t err = start_wifi_sta_mode();
    if (err != ESP_OK) {
      logging::errorf(wifi_tag, "Failed to start WiFi for provisioning: %s",
                      esp_err_to_name(err));
      return err;
    }

    logging::info("WiFi started for SmartConfig provisioning", wifi_tag);

    err = esp_event_handler_register(SC_EVENT, SC_EVENT_GOT_SSID_PSWD,
                                     &WifiService::provisioning_event_handler, this);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      logging::errorf(wifi_tag,
                      "Failed to register provisioning event handler: %s",
                      esp_err_to_name(err));
      return err;
    }

    err = esp_event_handler_register(SC_EVENT, SC_EVENT_SEND_ACK_DONE,
                                     &WifiService::provisioning_event_handler, this);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      esp_event_handler_unregister(SC_EVENT, SC_EVENT_GOT_SSID_PSWD,
                                   &WifiService::provisioning_event_handler);
      logging::errorf(wifi_tag, "Failed to register provisioning ACK handler: %s",
                      esp_err_to_name(err));
      return err;
    }

    // Use ESPTouch v1
    err = esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
    if (err != ESP_OK) {
      logging::warnf(wifi_tag, "Failed to set SmartConfig type: %s",
                     esp_err_to_name(err));
    }

    // Apply timeout if provided. API requires 15~255 seconds.
    if (opts.timeout_ms > 0) {
      uint32_t sec = opts.timeout_ms / 1000;
      if (sec < 15) sec = 15;
      if (sec > 255) sec = 255;
      esp_err_t to_err = esp_esptouch_set_timeout(static_cast<uint8_t>(sec));
      if (to_err != ESP_OK) {
        logging::warnf(wifi_tag, "Failed to set SmartConfig timeout: %s", esp_err_to_name(to_err));
      }
    }

    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    err = esp_smartconfig_start(&cfg);
    if (err != ESP_OK) {
      esp_event_handler_unregister(SC_EVENT, SC_EVENT_GOT_SSID_PSWD,
                                   &WifiService::provisioning_event_handler);
      esp_event_handler_unregister(SC_EVENT, SC_EVENT_SEND_ACK_DONE,
                                   &WifiService::provisioning_event_handler);
      logging::errorf(wifi_tag, "Failed to start SmartConfig: %s",
                      esp_err_to_name(err));
      return err;
    }

    provisioning_active.store(true);
    logging::info("SmartConfig provisioning started", wifi_tag);
    return ESP_OK;
  } else if (mode == ProvisionMode::SoftAP) {
    // SoftAP provisioning mode
    AccessPointConfig ap_cfg;
    ap_cfg.ssid = opts.ap_ssid;
    ap_cfg.channel = opts.ap_channel;
    ap_cfg.auth_mode = opts.ap_auth_mode;
    ap_cfg.max_connections = opts.ap_max_connections;

    WifiConfig updated_config = wifi_config;
    updated_config.ap_config = ap_cfg;
    esp_err_t err = config(updated_config);
    if (err != ESP_OK) {
      return err;
    }

    err = this->mode(WifiMode::AP);
    if (err != ESP_OK) {
      return err;
    }

    provisioning_active.store(true);
    logging::info("SoftAP provisioning started", wifi_tag);
    return ESP_OK;
  }

  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t WifiService::cancel_provisioning() {
  if (!provisioning_active.load()) {
    return ESP_OK;
  }

  if (current_provisioning_mode == ProvisionMode::SmartConfig) {
    esp_err_t err = esp_smartconfig_stop();
    if (err != ESP_OK) {
      logging::errorf(wifi_tag, "Failed to stop SmartConfig: %s",
                      esp_err_to_name(err));
      return err;
    }

    esp_event_handler_unregister(SC_EVENT, SC_EVENT_GOT_SSID_PSWD,
                                 &WifiService::provisioning_event_handler);
    esp_event_handler_unregister(SC_EVENT, SC_EVENT_SEND_ACK_DONE,
                                 &WifiService::provisioning_event_handler);
  }

  provisioning_active.store(false);
  logging::info("Provisioning cancelled", wifi_tag);
  return ESP_OK;
}

void WifiService::provisioning_event_handler(void *arg,
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
      wifi->on_provisioning_done(event_data);
    }
    break;
  case SC_EVENT_SEND_ACK_DONE:
    logging::info("Provisioning ACK sent to phone", wifi_tag);
    if (wifi->current_provisioning_mode == ProvisionMode::SmartConfig) {
      esp_err_t stop_err = esp_smartconfig_stop();
      if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
        logging::warnf(wifi_tag, "Failed to stop SmartConfig after ACK: %s", esp_err_to_name(stop_err));
      } else {
        logging::info("SmartConfig stopped after ACK", wifi_tag);
      }
      // Unregister handlers and finalize provisioning state
      esp_event_handler_unregister(SC_EVENT, SC_EVENT_GOT_SSID_PSWD,
                                   &WifiService::provisioning_event_handler);
      esp_event_handler_unregister(SC_EVENT, SC_EVENT_SEND_ACK_DONE,
                                   &WifiService::provisioning_event_handler);
      wifi->provisioning_active.store(false);
    }
    break;
  default:
    break;
  }
}

void WifiService::on_provisioning_done(void *event_data) {
  if (!event_data) {
    return;
  }

  const auto *event = static_cast<smartconfig_event_got_ssid_pswd_t *>(event_data);
  std::string ssid(reinterpret_cast<const char *>(event->ssid),
                   strnlen(reinterpret_cast<const char *>(event->ssid),
                           sizeof(event->ssid)));
  std::string passphrase(
      reinterpret_cast<const char *>(event->password),
      strnlen(reinterpret_cast<const char *>(event->password),
              sizeof(event->password)));

  logging::infof(wifi_tag,
                 "Provisioning received credentials: SSID='%s', passphrase_len=%zu",
                 ssid.c_str(), passphrase.size());

  WifiCredentials received{ssid, passphrase};
  esp_err_t validation_err = validate_station_config(received);
  if (validation_err != ESP_OK) {
    logging::error("Provisioning provided invalid credentials", wifi_tag);

    WifiEventData fail_event{};
    fail_event.mode = current_mode;
    fail_event.sta_connected = sta_connected.load();
    fail_event.sta_connecting = sta_connecting.load();
    fail_event.provisioning_active = provisioning_active.load();
    fail_event.event = WifiEvent::ProvisioningFailed;
    fail_event.error_code = validation_err;
    emit(fail_event);
    return;
  }

  temp_provisioning_credentials = WifiCredentials{ssid, passphrase};

  WifiEventData creds_event{};
  creds_event.mode = current_mode;
  creds_event.sta_connected = sta_connected.load();
  creds_event.sta_connecting = sta_connecting.load();
  creds_event.provisioning_active = provisioning_active.load();
  creds_event.event = WifiEvent::ProvisioningCredentialsReceived;
  creds_event.credentials = temp_provisioning_credentials;
  emit(creds_event);

  sta_connecting.store(true);

  wifi_config_t sta_cfg = make_sta_config(*temp_provisioning_credentials);
  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  if (err != ESP_OK) {
    logging::errorf(wifi_tag, "Failed to configure STA interface: %s",
                    esp_err_to_name(err));
    temp_provisioning_credentials.reset();

    WifiEventData fail_event{};
    fail_event.mode = current_mode;
    fail_event.sta_connected = sta_connected.load();
    fail_event.sta_connecting = sta_connecting.load();
    fail_event.provisioning_active = provisioning_active.load();
    fail_event.event = WifiEvent::ProvisioningFailed;
    fail_event.error_code = err;
    emit(fail_event);
    return;
  }

  err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
    logging::errorf(wifi_tag, "Failed to initiate connection: %s",
                    esp_err_to_name(err));
    temp_provisioning_credentials.reset();

    WifiEventData fail_event{};
    fail_event.mode = current_mode;
    fail_event.sta_connected = sta_connected.load();
    fail_event.sta_connecting = sta_connecting.load();
    fail_event.provisioning_active = provisioning_active.load();
    fail_event.event = WifiEvent::ProvisioningFailed;
    fail_event.error_code = err;
    emit(fail_event);
    return;
  }

  logging::info("Provisioning: Connection initiated, waiting for IP address...", wifi_tag);
}

void WifiService::emit(const WifiEventData &data) const {
  for (const auto &listener : listeners) {
    listener(data);
  }
}

void WifiService::emit_connection_failed(esp_err_t error) {
  sta_last_error = error;
  WifiEventData event_data{};
  event_data.mode = current_mode;
  event_data.sta_connected = sta_connected.load();
  event_data.sta_connecting = sta_connecting.load();
  event_data.provisioning_active = provisioning_active.load();
  event_data.event = WifiEvent::ConnectionFailed;
  event_data.error_code = error;
  emit(event_data);
}

void WifiService::on(EventListener listener) {
  listeners.push_back(std::move(listener));
}

WifiService &wifi() {
  static WifiService instance;
  return instance;
}

std::string ip_to_string(const esp_ip4_addr_t &ip) {
  auto *ip4 = reinterpret_cast<const ip4_addr_t *>(&ip);
  char buffer[16] = {0};
  ip4addr_ntoa_r(ip4, buffer, sizeof(buffer));
  return {buffer};
}

const char* wifi_event_to_string(WifiEvent event) {
  switch (event) {
    case WifiEvent::Connected: return "Connected";
    case WifiEvent::Disconnected: return "Disconnected";
    case WifiEvent::ConnectionFailed: return "ConnectionFailed";
    case WifiEvent::ProvisioningCredentialsReceived: return "ProvisioningCredentialsReceived";
    case WifiEvent::ProvisioningCompleted: return "ProvisioningCompleted";
    case WifiEvent::ProvisioningFailed: return "ProvisioningFailed";
    case WifiEvent::StateChanged: return "StateChanged";
    default: return "Unknown";
  }
}

const char* wifi_mode_to_string(WifiMode mode) {
  switch (mode) {
    case WifiMode::Off: return "Off";
    case WifiMode::STA: return "STA";
    case WifiMode::AP: return "AP";
    case WifiMode::APSTA: return "APSTA";
    default: return "Unknown";
  }
}

} // namespace earbrain
