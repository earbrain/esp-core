#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <functional>
#include <utility>

namespace earbrain::tasks {

template <typename Func>
esp_err_t run_detached(Func &&func, const char *name,
                       size_t stack_size = 4096,
                       UBaseType_t priority = 5) {
  auto *task_func = new std::function<void()>(std::forward<Func>(func));

  auto wrapper = [](void *param) {
    auto *f = static_cast<std::function<void()> *>(param);
    (*f)();
    delete f;
    vTaskDelete(nullptr);
  };

  BaseType_t result = xTaskCreate(wrapper, name, stack_size, task_func, priority, nullptr);
  if (result != pdPASS) {
    delete task_func;
    return ESP_FAIL;
  }
  return ESP_OK;
}

} // namespace earbrain::tasks
