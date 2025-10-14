#include "earbrain/mdns_service.hpp"
#include "earbrain/logging.hpp"

#include "esp_event.h"
#include "esp_netif.h"
#include "mdns.h"

namespace earbrain {

namespace {

constexpr const char mdns_tag[] = "mdns";

} // namespace

esp_err_t MdnsService::ensure_initialized() {
  if (initialized) {
    return ESP_OK;
  }

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }

  err = mdns_init();
  if (err == ESP_ERR_INVALID_STATE) {
    initialized = true;
    return ESP_OK;
  }
  if (err != ESP_OK) {
    return err;
  }

  initialized = true;
  return ESP_OK;
}

esp_err_t MdnsService::start(const MdnsConfig &config) {
  if (running) {
    const esp_err_t stop_err = stop();
    if (stop_err != ESP_OK) {
      return stop_err;
    }
  }

  MdnsConfig applied = config;

  esp_err_t err = ensure_initialized();
  if (err != ESP_OK) {
    return err;
  }

  err = mdns_hostname_set(applied.hostname.c_str());
  if (err != ESP_OK) {
    stop();
    return err;
  }

  err = mdns_instance_name_set(applied.instance_name.c_str());
  if (err != ESP_OK) {
    stop();
    return err;
  }

  err = mdns_service_add(nullptr, applied.service_type.c_str(),
                         applied.protocol.c_str(), applied.port, nullptr, 0);
  if (err != ESP_OK) {
    stop();
    return err;
  }

  mdns_config = std::move(applied);
  service_registered = true;
  registered_service_type = mdns_config.service_type;
  registered_protocol = mdns_config.protocol;
  running = true;

  logging::infof(mdns_tag,
                 "mDNS started: host=%s instance=%s service=%s protocol=%s port=%u",
                 mdns_config.hostname.c_str(),
                 mdns_config.instance_name.c_str(),
                 mdns_config.service_type.c_str(),
                 mdns_config.protocol.c_str(),
                 static_cast<unsigned>(mdns_config.port));

  return ESP_OK;
}

esp_err_t MdnsService::start() {
  return start(mdns_config);
}

esp_err_t MdnsService::stop() {
  if (!initialized) {
    running = false;
    return ESP_OK;
  }

  esp_err_t first_error = ESP_OK;

  if (service_registered && !registered_service_type.empty() &&
      !registered_protocol.empty()) {
    const esp_err_t err = mdns_service_remove(
        registered_service_type.c_str(),
        registered_protocol.c_str());
    if (err != ESP_OK && first_error == ESP_OK) {
      first_error = err;
    }
  }

  service_registered = false;
  registered_service_type.clear();
  registered_protocol.clear();

  mdns_free();
  initialized = false;
  running = false;

  return first_error;
}

MdnsService& mdns() {
  static MdnsService instance;
  return instance;
}

} // namespace earbrain
