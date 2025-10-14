#include "earbrain/logging.hpp"
#include "earbrain/metrics.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <vector>

static const char *TAG = "metrics_example";

extern "C" void app_main(void) {
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

  earbrain::logging::info("Demo completed. Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
