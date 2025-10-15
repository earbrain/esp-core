#include "earbrain/logging.hpp"
#include "earbrain/metrics.hpp"
#include "earbrain/wifi_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <cstring>

// Try to include credentials.h if it exists, otherwise fall back to sdkconfig
#if __has_include("../credentials.h")
#include "../credentials.h"
#endif

static const char *TAG = "wifi_test";

// Global event counter for tracking
static int event_count = 0;

// Helper to log status
void log_current_status() {
  auto status = earbrain::wifi().status();
  earbrain::logging::info("", TAG);
  earbrain::logging::info("Current WiFi Status:", TAG);
  earbrain::logging::infof(TAG, "  Mode: %s", earbrain::wifi_mode_to_string(status.mode));
  earbrain::logging::infof(TAG, "  STA Connected: %s", status.sta_connected ? "Yes" : "No");
  earbrain::logging::infof(TAG, "  STA Connecting: %s", status.sta_connecting ? "Yes" : "No");
  earbrain::logging::infof(TAG, "  Provisioning Active: %s", status.provisioning_active ? "Yes" : "No");

  if (status.sta_connected) {
    earbrain::logging::infof(TAG, "  STA IP: %s", earbrain::ip_to_string(status.sta_ip).c_str());
  }

  if (status.sta_last_disconnect_reason != WIFI_REASON_UNSPECIFIED) {
    earbrain::logging::infof(TAG, "  Last Disconnect Reason: %d",
                            static_cast<int>(status.sta_last_disconnect_reason));
  }

  if (status.sta_last_error != ESP_OK) {
    earbrain::logging::infof(TAG, "  Last Error: %s", esp_err_to_name(status.sta_last_error));
  }
}

// Helper to wait with status logging
void wait_and_log(uint32_t ms, const char* message = nullptr) {
  if (message) {
    earbrain::logging::infof(TAG, "Waiting %lums: %s", ms, message);
  }
  vTaskDelay(pdMS_TO_TICKS(ms));
}

// Test scenario functions
void test_sta_mode_basic() {
  earbrain::logging::info("", TAG);
  earbrain::logging::info("TEST 1: STA Mode Basic", TAG);
  earbrain::logging::info("Starting WiFi in STA mode...", TAG);
  esp_err_t err = earbrain::wifi().mode(earbrain::WifiMode::STA);
  earbrain::logging::infof(TAG, "Result: %s", esp_err_to_name(err));

  wait_and_log(1000, "Settling");
  log_current_status();
}

void test_ap_mode_basic() {
  earbrain::logging::info("", TAG);
  earbrain::logging::info("TEST 2: AP Mode Basic", TAG);

  earbrain::AccessPointConfig ap_config;
  ap_config.ssid = "esp-state-test-ap";
  ap_config.channel = 6;
  ap_config.auth_mode = WIFI_AUTH_OPEN;

  auto config = earbrain::wifi().config();
  config.ap_config = ap_config;

  esp_err_t err = earbrain::wifi().config(config);
  earbrain::logging::infof(TAG, "Config set: %s", esp_err_to_name(err));

  earbrain::logging::infof(TAG, "Starting AP mode: %s", ap_config.ssid.c_str());
  err = earbrain::wifi().mode(earbrain::WifiMode::AP);
  earbrain::logging::infof(TAG, "Result: %s", esp_err_to_name(err));

  wait_and_log(1000, "Settling");
  log_current_status();
}

void test_apsta_mode() {
  earbrain::logging::info("", TAG);
  earbrain::logging::info("TEST 3: APSTA Mode", TAG);
  earbrain::logging::info("Starting APSTA mode...", TAG);
  esp_err_t err = earbrain::wifi().mode(earbrain::WifiMode::APSTA);
  earbrain::logging::infof(TAG, "Result: %s", esp_err_to_name(err));

  wait_and_log(1000, "Settling");
  log_current_status();

  // Try to connect if credentials are available
  auto saved_creds = earbrain::wifi().load_credentials();
  if (saved_creds.has_value()) {
    earbrain::logging::infof(TAG, "Attempting STA connection in APSTA mode to: %s", saved_creds->ssid.c_str());
    err = earbrain::wifi().connect(saved_creds.value());
    earbrain::logging::infof(TAG, "Connect initiated: %s", esp_err_to_name(err));

    wait_and_log(5000, "Waiting for connection");
    log_current_status();
  } else {
    earbrain::logging::info("Skipping STA connection (no credentials configured)", TAG);
  }
}

void test_sta_to_ap_to_apsta_transition() {
  earbrain::logging::info("", TAG);
  earbrain::logging::info("TEST 4: Mode Transitions (STA -> AP -> APSTA -> STA)", TAG);

  // STA mode
  earbrain::logging::info("Step 1: Switch to STA mode", TAG);
  esp_err_t err = earbrain::wifi().mode(earbrain::WifiMode::STA);
  earbrain::logging::infof(TAG, "STA mode: %s", esp_err_to_name(err));
  wait_and_log(1000, "Settling");
  log_current_status();

  // AP mode
  earbrain::logging::info("Step 2: Switch to AP mode", TAG);
  err = earbrain::wifi().mode(earbrain::WifiMode::AP);
  earbrain::logging::infof(TAG, "AP mode: %s", esp_err_to_name(err));
  wait_and_log(1000, "Settling");
  log_current_status();

  // APSTA mode
  earbrain::logging::info("Step 3: Switch to APSTA mode", TAG);
  err = earbrain::wifi().mode(earbrain::WifiMode::APSTA);
  earbrain::logging::infof(TAG, "APSTA mode: %s", esp_err_to_name(err));
  wait_and_log(1000, "Settling");
  log_current_status();

  // Back to STA
  earbrain::logging::info("Step 4: Switch back to STA mode", TAG);
  err = earbrain::wifi().mode(earbrain::WifiMode::STA);
  earbrain::logging::infof(TAG, "STA mode: %s", esp_err_to_name(err));
  wait_and_log(1000, "Settling");
  log_current_status();
}

void test_smartconfig_lifecycle() {
  earbrain::logging::info("", TAG);
  earbrain::logging::info("TEST 5: SmartConfig Lifecycle", TAG);
  earbrain::logging::info("Starting SmartConfig provisioning...", TAG);
  earbrain::ProvisioningOptions opts;
  opts.timeout_ms = 30000;  // 30 seconds

  esp_err_t err = earbrain::wifi().start_provisioning(
      earbrain::ProvisionMode::SmartConfig, opts);
  earbrain::logging::infof(TAG, "Start provisioning: %s", esp_err_to_name(err));

  wait_and_log(2000, "SmartConfig listening");
  log_current_status();

  earbrain::logging::info("Cancelling SmartConfig provisioning...", TAG);
  err = earbrain::wifi().cancel_provisioning();
  earbrain::logging::infof(TAG, "Cancel provisioning: %s", esp_err_to_name(err));

  wait_and_log(1000, "Settling");
  log_current_status();
}

void test_error_cases() {
  earbrain::logging::info("", TAG);
  earbrain::logging::info("TEST 6: Error Cases", TAG);

  // Ensure we're in STA mode
  earbrain::wifi().mode(earbrain::WifiMode::STA);
  wait_and_log(1000);

  // Test 1: Non-existent SSID
  earbrain::logging::info("Test 6.1: Connecting to non-existent SSID", TAG);
  earbrain::WifiCredentials fake_creds{"NonExistentNetwork123456", "password123"};
  esp_err_t err = earbrain::wifi().connect(fake_creds);
  earbrain::logging::infof(TAG, "Connect initiated: %s", esp_err_to_name(err));

  wait_and_log(10000, "Waiting for connection failure");
  log_current_status();

  // Test 2: Invalid SSID (too long)
  earbrain::logging::info("Test 6.2: Invalid SSID (too long)", TAG);
  std::string long_ssid(33, 'X');  // 33 characters, max is 32
  earbrain::WifiCredentials invalid_creds{long_ssid, "password"};
  err = earbrain::wifi().connect(invalid_creds);
  earbrain::logging::infof(TAG, "Connect result: %s (expected error)", esp_err_to_name(err));

  wait_and_log(1000, "Settling");
  log_current_status();
}

void test_double_provisioning_start() {
  earbrain::logging::info("", TAG);
  earbrain::logging::info("TEST 7: Double Provisioning Start", TAG);
  earbrain::logging::info("Starting SmartConfig provisioning (1st time)...", TAG);
  esp_err_t err = earbrain::wifi().start_provisioning(earbrain::ProvisionMode::SmartConfig);
  earbrain::logging::infof(TAG, "1st start: %s", esp_err_to_name(err));

  wait_and_log(1000, "Settling");

  earbrain::logging::info("Starting SmartConfig provisioning (2nd time - should fail)...", TAG);
  err = earbrain::wifi().start_provisioning(earbrain::ProvisionMode::SmartConfig);
  earbrain::logging::infof(TAG, "2nd start: %s (expected ESP_ERR_INVALID_STATE)",
                          esp_err_to_name(err));

  wait_and_log(1000, "Settling");

  earbrain::logging::info("Cancelling SmartConfig provisioning...", TAG);
  err = earbrain::wifi().cancel_provisioning();
  earbrain::logging::infof(TAG, "Cancel: %s", esp_err_to_name(err));

  wait_and_log(1000, "Settling");
  log_current_status();
}

void test_mode_change_during_provisioning() {
  earbrain::logging::info("", TAG);
  earbrain::logging::info("TEST 8: Mode Change During Provisioning", TAG);
  earbrain::logging::info("Starting SmartConfig provisioning...", TAG);
  esp_err_t err = earbrain::wifi().start_provisioning(earbrain::ProvisionMode::SmartConfig);
  earbrain::logging::infof(TAG, "Start provisioning: %s", esp_err_to_name(err));

  wait_and_log(2000, "Provisioning active");
  log_current_status();

  earbrain::logging::info("Attempting to change mode to AP while provisioning...", TAG);
  err = earbrain::wifi().mode(earbrain::WifiMode::AP);
  earbrain::logging::infof(TAG, "Mode change: %s", esp_err_to_name(err));

  wait_and_log(1000, "Settling");
  log_current_status();

  // Clean up
  earbrain::logging::info("Cancelling provisioning...", TAG);
  earbrain::wifi().cancel_provisioning();
  wait_and_log(1000);
}

extern "C" void app_main(void) {
  earbrain::logging::info("", TAG);
  earbrain::logging::info("WiFi Test Suite", TAG);
  earbrain::logging::info("", TAG);

  // Save credentials from credentials.h if available
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
  earbrain::logging::infof(TAG, "Saving credentials for: %s", WIFI_SSID);
  esp_err_t save_err = earbrain::wifi().save_credentials(WIFI_SSID, WIFI_PASSWORD);
  if (save_err != ESP_OK) {
    earbrain::logging::errorf(TAG, "Failed to save credentials: %s", esp_err_to_name(save_err));
  }
#endif

  // Register comprehensive event listener to track ALL events
  earbrain::wifi().on([](const earbrain::WifiEventData& event) {
    event_count++;

    earbrain::logging::info("", TAG);
    earbrain::logging::infof(TAG, "EVENT #%d: %s", event_count,
                            earbrain::wifi_event_to_string(event.event));
    earbrain::logging::infof(TAG, "  Mode: %s", earbrain::wifi_mode_to_string(event.mode));
    earbrain::logging::infof(TAG, "  Connected: %s, Connecting: %s, Provisioning: %s",
                            event.sta_connected ? "Yes" : "No",
                            event.sta_connecting ? "Yes" : "No",
                            event.provisioning_active ? "Yes" : "No");

    switch (event.event) {
      case earbrain::WifiEvent::Connected:
        if (event.ip_address.has_value()) {
          earbrain::logging::infof(TAG, "  IP Address: %s",
                                  earbrain::ip_to_string(event.ip_address.value()).c_str());
        }
        break;

      case earbrain::WifiEvent::Disconnected:
        if (event.disconnect_reason.has_value()) {
          earbrain::logging::infof(TAG, "  Reason: %d",
                                  static_cast<int>(event.disconnect_reason.value()));
        }
        break;

      case earbrain::WifiEvent::ConnectionFailed:
        earbrain::logging::infof(TAG, "  Error: %s",
                                esp_err_to_name(event.error_code));
        break;

      case earbrain::WifiEvent::ProvisioningCredentialsReceived:
        if (event.credentials.has_value()) {
          earbrain::logging::infof(TAG, "  SSID: %s",
                                  event.credentials->ssid.c_str());
        }
        break;

      case earbrain::WifiEvent::ProvisioningCompleted:
        earbrain::logging::info("  Provisioning completed successfully!", TAG);
        break;

      case earbrain::WifiEvent::ProvisioningFailed:
        earbrain::logging::infof(TAG, "  Provisioning failed: %s",
                                esp_err_to_name(event.error_code));
        break;

      case earbrain::WifiEvent::StateChanged:
        // State is already logged above
        break;

      default:
        break;
    }
  });

  // Initial status
  log_current_status();

  // Run all tests
  test_sta_mode_basic();
  wait_and_log(2000, "Between tests");

  test_ap_mode_basic();
  wait_and_log(2000, "Between tests");

  test_apsta_mode();
  wait_and_log(2000, "Between tests");

  test_sta_to_ap_to_apsta_transition();
  wait_and_log(2000, "Between tests");

  test_smartconfig_lifecycle();
  wait_and_log(2000, "Between tests");

  test_error_cases();
  wait_and_log(2000, "Between tests");

  test_double_provisioning_start();
  wait_and_log(2000, "Between tests");

  test_mode_change_during_provisioning();
  wait_and_log(2000, "Between tests");

  // Final summary
  earbrain::logging::info("", TAG);
  earbrain::logging::info("Test Suite Complete!", TAG);
  earbrain::logging::infof(TAG, "Total events captured: %d", event_count);
  earbrain::logging::info("", TAG);

  log_current_status();

  earbrain::logging::info("Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes, Events: %d",
                            metrics.heap_free, event_count);
  }
}
