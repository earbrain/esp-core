#include "earbrain/logging.hpp"
#include "earbrain/mdns_service.hpp"
#include "earbrain/metrics.hpp"
#include "earbrain/task_helpers.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

static const char *TAG = "core_example";

static const char *wifi_mode_string(earbrain::WifiMode mode) {
  switch (mode) {
  case earbrain::WifiMode::STA:
    return "STA";
  case earbrain::WifiMode::AP:
    return "AP";
  case earbrain::WifiMode::APSTA:
    return "APSTA";
  case earbrain::WifiMode::Off:
  default:
    return "Off";
  }
}

static void example_logging() {
  earbrain::logging::info("=== Logging Demo ===", TAG);

  earbrain::logging::info("Basic logging", TAG);
  earbrain::logging::debug("Debug message", TAG);
  earbrain::logging::warn("Warning message", TAG);
  earbrain::logging::error("Error message", TAG);

  earbrain::logging::infof(TAG, "Formatted: %d + %d = %d", 1, 2, 3);

  auto batch = earbrain::logging::collect(0, 10);
  earbrain::logging::infof(TAG, "Collected %zu log entries", batch.entries.size());
}

static void example_wifi() {
  earbrain::logging::info("=== WiFi & mDNS Service Demo ===", TAG);

  earbrain::AccessPointConfig ap_config;
  ap_config.ssid = "esp-core-demo";
  ap_config.channel = 6;
  ap_config.auth_mode = WIFI_AUTH_OPEN;

  auto config = earbrain::wifi().config();
  config.ap_config = ap_config;

  esp_err_t err = earbrain::wifi().config(config);
  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
    return;
  }

  err = earbrain::wifi().mode(earbrain::WifiMode::APSTA);
  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to set mode to APSTA: %s", esp_err_to_name(err));
    return;
  }

  earbrain::logging::infof(TAG, "AP started: %s", ap_config.ssid.c_str());

  vTaskDelay(pdMS_TO_TICKS(2000));

  auto status = earbrain::wifi().status();
  earbrain::logging::infof(TAG, "WiFi mode: %s", wifi_mode_string(status.mode));
  earbrain::logging::infof(TAG, "STA connected: %s", status.sta_connected ? "Yes" : "No");
  earbrain::logging::infof(TAG, "STA connecting: %s", status.sta_connecting ? "Yes" : "No");
  earbrain::logging::infof(TAG, "Provisioning active: %s",
                                 status.provisioning_active ? "Yes" : "No");

  if (status.sta_connected) {
    earbrain::logging::infof(TAG, "STA IP: %s", earbrain::ip_to_string(status.sta_ip).c_str());
  } else if (status.sta_last_disconnect_reason != WIFI_REASON_UNSPECIFIED) {
    earbrain::logging::infof(TAG, "Last disconnect reason: %d",
                                   static_cast<int>(status.sta_last_disconnect_reason));
  }

  if (status.sta_last_error != ESP_OK) {
    earbrain::logging::infof(TAG, "Last error: %s", esp_err_to_name(status.sta_last_error));
  }

  earbrain::logging::info("Performing WiFi scan...", TAG);
  auto scan_result = earbrain::wifi().perform_scan();

  if (scan_result.error == ESP_OK) {
    earbrain::logging::infof(TAG, "Found %zu networks", scan_result.networks.size());
    for (size_t i = 0; i < scan_result.networks.size() && i < 5; i++) {
      const auto &net = scan_result.networks[i];
      earbrain::logging::infof(TAG, "  %s (Signal: %d%%)", net.ssid.c_str(), net.signal);
    }
  }

  // Start mDNS service
  earbrain::logging::info("Starting mDNS service...", TAG);
  earbrain::MdnsConfig mdns_config;
  mdns_config.hostname = "esp-core-device";
  mdns_config.instance_name = "ESP Core Demo";
  mdns_config.service_type = "_http";
  mdns_config.protocol = "_tcp";
  mdns_config.port = 80;

  err = earbrain::mdns().start(mdns_config);

  if (err == ESP_OK) {
    earbrain::logging::info("mDNS started successfully!", TAG);
    earbrain::logging::infof(TAG, "Discoverable as: %s.local", mdns_config.hostname.c_str());
    earbrain::logging::infof(TAG, "Service: %s%s:%u",
                                   mdns_config.service_type.c_str(),
                                   mdns_config.protocol.c_str(),
                                   mdns_config.port);
  } else {
    earbrain::logging::errorf(TAG, "Failed to start mDNS: %s", esp_err_to_name(err));
  }
}

static void example_tasks() {
  earbrain::logging::info("=== Task Helpers Demo ===", TAG);

  static std::atomic<int> counter{0};

  earbrain::logging::info("Creating simple task...", TAG);
  earbrain::tasks::run_detached([]() {
    earbrain::logging::info("Hello from detached task!", "task_1");
    vTaskDelay(pdMS_TO_TICKS(500));
  }, "simple_task");

  vTaskDelay(pdMS_TO_TICKS(1000));

  earbrain::logging::info("Creating 3 parallel tasks...", TAG);
  for (int i = 0; i < 3; i++) {
    earbrain::tasks::run_detached([i]() {
      int count = counter.fetch_add(1);
      char task_name[32];
      snprintf(task_name, sizeof(task_name), "parallel_%d", i);
      earbrain::logging::infof(task_name, "Counter: %d", count);
      vTaskDelay(pdMS_TO_TICKS(300));
    }, "parallel_task");
  }

  vTaskDelay(pdMS_TO_TICKS(1500));
  earbrain::logging::infof(TAG, "Final counter: %d", counter.load());
}

static void example_metrics() {
  earbrain::logging::info("=== System Metrics Demo ===", TAG);

  auto metrics = earbrain::collect_metrics();
  earbrain::logging::infof(TAG, "Heap Total:  %lu bytes", metrics.heap_total);
  earbrain::logging::infof(TAG, "Heap Free:   %lu bytes", metrics.heap_free);
  earbrain::logging::infof(TAG, "Heap Used:   %lu bytes", metrics.heap_used);
  earbrain::logging::infof(TAG, "Min Free:    %lu bytes", metrics.heap_min_free);
  earbrain::logging::infof(TAG, "Largest:     %lu bytes", metrics.heap_largest_free_block);

  if (metrics.heap_total > 0) {
    float used_percent = (static_cast<float>(metrics.heap_used) / metrics.heap_total) * 100.0f;
    earbrain::logging::infof(TAG, "Usage:       %.1f%%", used_percent);
  }

  earbrain::logging::info("Allocating 10KB...", TAG);
  auto before = earbrain::collect_metrics();
  {
    std::vector<uint8_t> buffer(10000);
    for (auto &b : buffer) b = 0xFF;
    auto after = earbrain::collect_metrics();
    long change = static_cast<long>(after.heap_used - before.heap_used);
    earbrain::logging::infof(TAG, "Heap change: %ld bytes", change);
  }
  auto released = earbrain::collect_metrics();
  earbrain::logging::infof(TAG, "After release: %lu bytes free", released.heap_free);
}

static void example_smartconfig() {
  earbrain::logging::info("=== SmartConfig Demo ===", TAG);
  earbrain::logging::info("", TAG);
  earbrain::logging::info("Instructions:", TAG);
  earbrain::logging::info("1. Install ESPTouch app on your smartphone", TAG);
  earbrain::logging::info("   - iOS: https://apps.apple.com/app/espressif-esptouch/id1071176700", TAG);
  earbrain::logging::info("   - Android: Search 'ESPTouch' on Google Play", TAG);
  earbrain::logging::info("2. Connect your phone to the WiFi network you want to configure", TAG);
  earbrain::logging::info("3. Open ESPTouch app and enter your WiFi password", TAG);
  earbrain::logging::info("4. Tap 'Confirm' to start provisioning", TAG);
  earbrain::logging::info("", TAG);

  earbrain::wifi().on([](const earbrain::WifiEventData& event) {
    switch (event.event) {
      case earbrain::WifiEvent::ProvisioningCredentialsReceived:
        if (event.credentials.has_value()) {
          earbrain::logging::infof(TAG, "Received credentials for SSID: %s",
                                   event.credentials->ssid.c_str());
          earbrain::logging::info("Attempting to connect...", TAG);
        }
        break;

      case earbrain::WifiEvent::ProvisioningCompleted:
        if (event.ip_address.has_value()) {
          earbrain::logging::info("Provisioning completed! Credentials saved.", TAG);
          earbrain::logging::infof(TAG, "IP Address: %s",
                                   earbrain::ip_to_string(event.ip_address.value()).c_str());
        }
        break;

      case earbrain::WifiEvent::ProvisioningFailed:
        earbrain::logging::errorf(TAG, "Provisioning failed: %s",
                                  esp_err_to_name(event.error_code));
        break;

      case earbrain::WifiEvent::Connected:
        if (event.ip_address.has_value()) {
          earbrain::logging::infof(TAG, "Connected! IP: %s",
                                   earbrain::ip_to_string(event.ip_address.value()).c_str());
        }
        break;

      default:
        break;
    }
  });

  earbrain::logging::info("Starting SmartConfig provisioning...", TAG);
  esp_err_t err = earbrain::wifi().start_provisioning(earbrain::ProvisionMode::SmartConfig);
  if (err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to start provisioning: %s", esp_err_to_name(err));
    return;
  }

  earbrain::logging::info("Provisioning started successfully!", TAG);
  earbrain::logging::info("Waiting for WiFi credentials (timeout: 120 seconds)...", TAG);

  constexpr uint32_t timeout_ms = 120000;
  constexpr uint32_t check_interval_ms = 500;
  uint32_t elapsed_ms = 0;

  while (elapsed_ms < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
    elapsed_ms += check_interval_ms;

    const auto status = earbrain::wifi().status();
    if (!status.provisioning_active ||
        status.sta_connected ||
        status.sta_last_error != ESP_OK) {
      break;
    }
  }

  const auto status = earbrain::wifi().status();
  if (status.sta_connected) {
    // Clean up provisioning resources immediately on success
    earbrain::wifi().cancel_provisioning();

    earbrain::logging::info("Provisioning completed successfully!", TAG);

    if (status.sta_ip.addr != 0) {
      earbrain::logging::infof(TAG, "Assigned IP: %s",
                               earbrain::ip_to_string(status.sta_ip).c_str());
    }

    auto credentials = earbrain::wifi().load_credentials();
    if (credentials.has_value()) {
      earbrain::logging::infof(TAG, "Saved SSID: %s", credentials->ssid.c_str());
    }
  } else if (status.sta_last_error != ESP_OK) {
    earbrain::logging::errorf(TAG, "Provisioning completed with error: %s",
                              esp_err_to_name(status.sta_last_error));
    if (status.sta_last_disconnect_reason != WIFI_REASON_UNSPECIFIED) {
      earbrain::logging::infof(TAG, "Disconnect reason: %d",
                               static_cast<int>(status.sta_last_disconnect_reason));
    }
  } else if (elapsed_ms >= timeout_ms) {
    earbrain::logging::warn("Provisioning timed out. No credentials received.", TAG);
  } else {
    earbrain::logging::warn("Provisioning stopped before completion.", TAG);
  }

  // Clean up provisioning if not already done (idempotent)
  earbrain::wifi().cancel_provisioning();
}

extern "C" void app_main(void) {
  earbrain::logging::info("ESP Core Example Started", TAG);
  earbrain::logging::infof(TAG, "Core version: %s", CORE_VERSION);

#if defined(EXAMPLE_LOGGING)
  example_logging();
#elif defined(EXAMPLE_WIFI)
  example_wifi();
#elif defined(EXAMPLE_TASKS)
  example_tasks();
#elif defined(EXAMPLE_METRICS)
  example_metrics();
#elif defined(EXAMPLE_SMARTCONFIG)
  example_smartconfig();
#else
  // Default: run all examples
  earbrain::logging::info("Running all examples...", TAG);
  example_logging();
  vTaskDelay(pdMS_TO_TICKS(2000));
  example_tasks();
  vTaskDelay(pdMS_TO_TICKS(2000));
  example_metrics();
  vTaskDelay(pdMS_TO_TICKS(2000));
  example_wifi();
#endif

  earbrain::logging::info("Demo completed. Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
