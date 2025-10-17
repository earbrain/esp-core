#pragma once

#include "esp_err.h"
#include <string>

namespace earbrain {

struct MdnsConfig {
  std::string hostname = "esp-device";
  std::string instance_name = "ESP Device";
  std::string service_type = "_http";
  std::string protocol = "_tcp";
  uint16_t port = 80;
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
