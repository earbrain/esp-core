#include "earbrain/logging.hpp"
#include "earbrain/metrics.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "logging_example";

extern "C" void app_main(void) {
  earbrain::logging::info("=== Logging Demo ===", TAG);

  earbrain::logging::info("Basic logging", TAG);
  earbrain::logging::debug("Debug message", TAG);
  earbrain::logging::warn("Warning message", TAG);
  earbrain::logging::error("Error message", TAG);

  earbrain::logging::infof(TAG, "Formatted: %d + %d = %d", 1, 2, 3);

  auto batch = earbrain::logging::collect(0, 10);
  earbrain::logging::infof(TAG, "Collected %zu log entries", batch.entries.size());

  earbrain::logging::info("Demo completed. Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
