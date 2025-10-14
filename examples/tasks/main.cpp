#include "earbrain/logging.hpp"
#include "earbrain/metrics.hpp"
#include "earbrain/task_helpers.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

static const char *TAG = "tasks_example";

extern "C" void app_main(void) {
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

  earbrain::logging::info("Demo completed. Running idle loop...", TAG);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    auto metrics = earbrain::collect_metrics();
    earbrain::logging::infof(TAG, "Heartbeat - Free heap: %lu bytes", metrics.heap_free);
  }
}
