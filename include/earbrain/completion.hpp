#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <optional>

namespace earbrain {

template <typename T>
class Completion {
public:
  Completion() : semaphore(xSemaphoreCreateBinary()), result{} {}

  ~Completion() {
    if (semaphore) {
      vSemaphoreDelete(semaphore);
    }
  }

  Completion(const Completion &) = delete;
  Completion &operator=(const Completion &) = delete;
  Completion(Completion &&) = delete;
  Completion &operator=(Completion &&) = delete;

  void complete(T value) {
    result = value;
    if (semaphore) {
      xSemaphoreGive(semaphore);
    }
  }

  std::optional<T> wait(uint32_t timeout_ms = portMAX_DELAY) {
    if (!semaphore) {
      return std::nullopt;
    }

    TickType_t ticks = (timeout_ms == portMAX_DELAY)
                       ? portMAX_DELAY
                       : pdMS_TO_TICKS(timeout_ms);

    if (xSemaphoreTake(semaphore, ticks) == pdTRUE) {
      return result;
    }

    return std::nullopt;
  }

  bool is_complete() const {
    if (!semaphore) {
      return false;
    }
    return uxSemaphoreGetCount(semaphore) > 0;
  }

private:
  SemaphoreHandle_t semaphore;
  T result;
};

} // namespace earbrain