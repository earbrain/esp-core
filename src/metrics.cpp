#include "earbrain/metrics.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"

namespace earbrain {

Metrics collect_metrics() {
  Metrics metrics{};

  metrics.heap_total =
      static_cast<std::uint32_t>(heap_caps_get_total_size(MALLOC_CAP_8BIT));
  metrics.heap_free =
      static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
  metrics.heap_used = metrics.heap_total > metrics.heap_free
                        ? metrics.heap_total - metrics.heap_free
                        : 0;
  metrics.heap_min_free = static_cast<std::uint32_t>(
      heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
  metrics.heap_largest_free_block = static_cast<std::uint32_t>(
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  metrics.timestamp_ms =
      static_cast<std::uint64_t>(esp_timer_get_time() / 1000);

  return metrics;
}

} // namespace earbrain
