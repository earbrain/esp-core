#pragma once

#include "esp_err.h"
#include <string>

namespace earbrain {

/**
 * @brief mDNS service configuration
 */
struct MdnsConfig {
  std::string hostname = "esp-device";      ///< mDNS hostname (e.g., "esp-device.local")
  std::string instance_name = "ESP Device"; ///< Human-readable instance name
  std::string service_type = "_http";       ///< Service type (e.g., "_http", "_ftp")
  std::string protocol = "_tcp";            ///< Protocol ("_tcp" or "_udp")
  uint16_t port = 80;                       ///< Service port number
};

/**
 * @brief mDNS service manager
 *
 * Manages mDNS responder for device discovery on local networks.
 * Allows devices to be discovered by name (e.g., "esp-device.local")
 * instead of requiring IP addresses.
 */
class MdnsService {
public:
  MdnsService() = default;
  MdnsService(const MdnsConfig &config) : mdns_config(config) {}

  /**
   * @brief Start mDNS service with given configuration
   *
   * @param config mDNS configuration
   * @return ESP_OK on success, error code otherwise
   */
  esp_err_t start(const MdnsConfig &config);

  /**
   * @brief Start mDNS service with stored configuration
   *
   * @return ESP_OK on success, error code otherwise
   */
  esp_err_t start();

  /**
   * @brief Stop mDNS service
   *
   * @return ESP_OK on success, error code otherwise
   */
  esp_err_t stop();

  /**
   * @brief Check if mDNS service is running
   *
   * @return true if running, false otherwise
   */
  bool is_running() const noexcept { return running; }

  /**
   * @brief Get current configuration
   *
   * @return Reference to current MdnsConfig
   */
  const MdnsConfig &config() const noexcept { return mdns_config; }

private:
  esp_err_t ensure_initialized();

  MdnsConfig mdns_config;
  bool initialized = false;
  bool service_registered = false;
  bool running = false;
  std::string registered_service_type;
  std::string registered_protocol;
};

} // namespace earbrain
