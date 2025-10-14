#pragma once

#include <cstdint>

namespace earbrain {

struct Metrics {
  std::uint32_t heap_total;
  std::uint32_t heap_free;
  std::uint32_t heap_used;
  std::uint32_t heap_min_free;
  std::uint32_t heap_largest_free_block;
  std::uint64_t timestamp_ms;
};

Metrics collect_metrics();

} // namespace earbrain
