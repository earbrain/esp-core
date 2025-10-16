#pragma once

#include "esp_err.h"
#include <string>

namespace earbrain {

/**
 * @brief mDNS service configuration
 */
struct MdnsConfig {
  std::string hostname =
      "esp-device"; ///< mDNS hostname (e.g., "esp-device.local")
  std::string instance_name = "ESP Device"; ///< Human-readable instance name
  std::string service_type = "_http"; ///< Service type (e.g., "_http", "_ftp")
  std::string protocol = "_tcp";      ///< Protocol ("_tcp" or "_udp")
  uint16_t port = 80;                 ///< Service port number
};

class MdnsService {
public:
  MdnsService() = default;

  MdnsService(const MdnsService &) = delete;
  MdnsService &operator=(const MdnsService &) = delete;
  MdnsService(MdnsService &&) = delete;
  MdnsService &operator=(MdnsService &&) = delete;

  esp_err_t initialize();
  esp_err_t start(const MdnsConfig &config);
  esp_err_t start();
  esp_err_t stop();
  bool is_running() const noexcept { return running; }
  const MdnsConfig &config() const noexcept { return mdns_config; }

private:

  MdnsConfig mdns_config;
  bool initialized = false;
  bool service_registered = false;
  bool running = false;
  std::string registered_service_type;
  std::string registered_protocol;
};

MdnsService &mdns();

} // namespace earbrain
