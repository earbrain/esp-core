#include "earbrain/logging.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace earbrain::logging {

namespace {

std::string normalise_tag(std::string_view tag) {
  if (tag.empty()) {
    return std::string(Logger::default_tag());
  }
  return std::string(tag);
}

std::string format_message(const char *format, va_list args) {
  if (!format) {
    return {};
  }

  va_list copy;
  va_copy(copy, args);
  const int required = vsnprintf(nullptr, 0, format, copy);
  va_end(copy);
  if (required <= 0) {
    return {};
  }

  std::vector<char> buffer(static_cast<std::size_t>(required) + 1);
  va_list second;
  va_copy(second, args);
  vsnprintf(buffer.data(), buffer.size(), format, second);
  va_end(second);
  return std::string(buffer.data(), static_cast<std::size_t>(required));
}

} // namespace

LogStore::LogStore() : entries(), next_id(0) {}

void LogStore::log(esp_log_level_t level, std::string_view tag,
                   std::string message) {
  const uint32_t timestamp_ms = esp_log_timestamp();

  std::lock_guard<std::mutex> lock(mutex);
  LogEntry entry;
  entry.id = next_id++;
  entry.timestamp_ms = timestamp_ms;
  entry.level = level;
  entry.tag = std::string(tag);
  entry.message = std::move(message);
  entries.push_back(std::move(entry));
  if (entries.size() > max_entries) {
    entries.pop_front();
  }
}

LogBatch LogStore::collect(uint64_t cursor, std::size_t limit) const {
  LogBatch batch;
  const std::size_t effective_limit =
      std::clamp<std::size_t>(limit, std::size_t{1}, max_entries);

  std::lock_guard<std::mutex> lock(mutex);
  if (entries.empty()) {
    batch.next_cursor = cursor;
    batch.has_more = false;
    return batch;
  }

  batch.entries.reserve(std::min(effective_limit, entries.size()));
  const bool skip_by_cursor = cursor > 0;

  for (const auto &entry : entries) {
    if (skip_by_cursor && entry.id <= cursor) {
      continue;
    }
    batch.entries.push_back(entry);
    if (batch.entries.size() >= effective_limit) {
      break;
    }
  }

  const uint64_t cursor_reference =
      batch.entries.empty() ? cursor : batch.entries.back().id;
  batch.next_cursor = cursor_reference;
  batch.has_more = entries.back().id > cursor_reference;

  return batch;
}

void LogStore::clear() {
  std::lock_guard<std::mutex> lock(mutex);
  entries.clear();
}

Logger &Logger::instance() {
  static Logger logger;
  return logger;
}

Logger::Logger() : store() {}

void Logger::info(std::string_view message, std::string_view tag) {
  write(ESP_LOG_INFO, tag, message);
}

void Logger::warn(std::string_view message, std::string_view tag) {
  write(ESP_LOG_WARN, tag, message);
}

void Logger::error(std::string_view message, std::string_view tag) {
  write(ESP_LOG_ERROR, tag, message);
}

void Logger::debug(std::string_view message, std::string_view tag) {
  write(ESP_LOG_DEBUG, tag, message);
}

void Logger::infof(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    info(message);
  }
}

void Logger::infof(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    info(message, tag ? std::string_view{tag} : default_tag());
  }
}

void Logger::warnf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    warn(message);
  }
}

void Logger::warnf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    warn(message, tag ? std::string_view{tag} : default_tag());
  }
}

void Logger::errorf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    error(message);
  }
}

void Logger::errorf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    error(message, tag ? std::string_view{tag} : default_tag());
  }
}

void Logger::debugf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    debug(message);
  }
}

void Logger::debugf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    debug(message, tag ? std::string_view{tag} : default_tag());
  }
}

LogBatch Logger::collect(uint64_t cursor, std::size_t limit) const {
  return store.collect(cursor, limit);
}

void Logger::clear() { store.clear(); }

void Logger::write(esp_log_level_t level, std::string_view tag,
                   std::string_view message) {
  const std::string tag_copy = normalise_tag(tag);
  const std::string message_copy(message);

  store.log(level, tag_copy, message_copy);
  std::string line_for_stdout = message_copy;
  if (line_for_stdout.empty() || line_for_stdout.back() != '\n') {
    line_for_stdout.push_back('\n');
  }
  esp_log_write(level, tag_copy.c_str(), "%s", line_for_stdout.c_str());
}

Logger &get_logger() { return Logger::instance(); }

void info(std::string_view message, std::string_view tag) {
  get_logger().info(message, tag);
}

void warn(std::string_view message, std::string_view tag) {
  get_logger().warn(message, tag);
}

void error(std::string_view message, std::string_view tag) {
  get_logger().error(message, tag);
}

void debug(std::string_view message, std::string_view tag) {
  get_logger().debug(message, tag);
}

void infof(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    info(message);
  }
}

void infof(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    info(message, tag ? std::string_view{tag} : Logger::default_tag());
  }
}

void warnf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    warn(message);
  }
}

void warnf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    warn(message, tag ? std::string_view{tag} : Logger::default_tag());
  }
}

void errorf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    error(message);
  }
}

void errorf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    error(message, tag ? std::string_view{tag} : Logger::default_tag());
  }
}

void debugf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    debug(message);
  }
}

void debugf(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);
  if (!message.empty()) {
    debug(message, tag ? std::string_view{tag} : Logger::default_tag());
  }
}

LogBatch collect(uint64_t cursor, std::size_t limit) {
  return get_logger().collect(cursor, limit);
}

void clear() { get_logger().clear(); }

} // namespace earbrain::logging
