#include "earbrain/logging.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "smartconfig_example";

extern "C" void app_main(void) {
  earbrain::logging::info("SmartConfig demo", TAG);

  // Listen for Wi-Fi events and cancel provisioning after ACK is sent
  earbrain::wifi().on([](const earbrain::WifiEventData &e) {
    switch (e.event) {
    case earbrain::WifiEvent::Connected: {
      if (e.ip_address.has_value()) {
        earbrain::logging::infof(TAG, "Connected. IP: %s",
                                 earbrain::ip_to_string(e.ip_address.value()).c_str());
      } else {
        earbrain::logging::info("Connected.", TAG);
      }
      // Do not cancel here; wait for provisioning completion/ACK
      break;
    }
    case earbrain::WifiEvent::ProvisioningCompleted: {
      // Credentials verified and saved; wait for ACK (ProvAck state) before cancel
      if (e.ip_address.has_value()) {
        earbrain::logging::infof(TAG, "Provisioning completed. IP: %s",
                                 earbrain::ip_to_string(e.ip_address.value()).c_str());
      } else {
        earbrain::logging::info("Provisioning completed.", TAG);
      }
      break;
    }
    case earbrain::WifiEvent::StateChanged: {
      // When SmartConfig ACK has been sent, service enters ProvAck state
      if (e.state == earbrain::WifiState::ProvAck) {
        earbrain::logging::info("ACK sent. Stopping provisioning.", TAG);
        // Give the phone app a brief moment to process the ACK before stopping
        vTaskDelay(pdMS_TO_TICKS(500));
        earbrain::wifi().cancel_provisioning();
      }
      break;
    }
    case earbrain::WifiEvent::ConnectionFailed:
    case earbrain::WifiEvent::ProvisioningFailed:
      earbrain::logging::errorf(TAG, "Provisioning or connection failed: %s", esp_err_to_name(e.error_code));
      // Stop SmartConfig on failure as well
      earbrain::wifi().cancel_provisioning();
      break;
    default:
      break;
    }
  });

  // Start SmartConfig provisioning
  esp_err_t err = earbrain::wifi().start_provisioning(earbrain::ProvisionMode::SmartConfig);
  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to start provisioning: %s", esp_err_to_name(err));
    return;
  }

  // Idle: events will drive behavior
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
