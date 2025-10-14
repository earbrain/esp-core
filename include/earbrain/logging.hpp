#pragma once

#include "esp_log.h"

#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace earbrain::logging {

struct LogEntry {
  uint64_t id = 0;
  uint32_t timestamp_ms = 0;
  esp_log_level_t level = ESP_LOG_INFO;
  std::string tag;
  std::string message;
};

struct LogBatch {
  std::vector<LogEntry> entries;
  uint64_t next_cursor = 0;
  bool has_more = false;
};

class LogStore {
public:
  LogStore();

  void log(esp_log_level_t level, std::string_view tag, std::string message);
  LogBatch collect(uint64_t cursor, std::size_t limit) const;
  void clear();

  static constexpr std::size_t max_entries = 1024;

private:
  mutable std::mutex mutex;
  std::deque<LogEntry> entries;
  uint64_t next_id;
};

class Logger {
public:
  static Logger &instance();

  void info(std::string_view message, std::string_view tag = default_tag());
  void warn(std::string_view message, std::string_view tag = default_tag());
  void error(std::string_view message, std::string_view tag = default_tag());
  void debug(std::string_view message, std::string_view tag = default_tag());

  void infof(const char *format, ...);
  void infof(const char *tag, const char *format, ...);
  void warnf(const char *format, ...);
  void warnf(const char *tag, const char *format, ...);
  void errorf(const char *format, ...);
  void errorf(const char *tag, const char *format, ...);
  void debugf(const char *format, ...);
  void debugf(const char *tag, const char *format, ...);

  LogBatch collect(uint64_t cursor, std::size_t limit) const;
  void clear();

  static constexpr std::string_view default_tag() { return "core"; }

private:
  Logger();

  void write(esp_log_level_t level, std::string_view tag,
             std::string_view message);

  LogStore store;
};

Logger &get_logger();

void info(std::string_view message, std::string_view tag = Logger::default_tag());
void warn(std::string_view message, std::string_view tag = Logger::default_tag());
void error(std::string_view message, std::string_view tag = Logger::default_tag());
void debug(std::string_view message, std::string_view tag = Logger::default_tag());

void infof(const char *format, ...);
void infof(const char *tag, const char *format, ...);
void warnf(const char *format, ...);
void warnf(const char *tag, const char *format, ...);
void errorf(const char *format, ...);
void errorf(const char *tag, const char *format, ...);
void debugf(const char *format, ...);
void debugf(const char *tag, const char *format, ...);

LogBatch collect(uint64_t cursor, std::size_t limit);
void clear();

} // namespace earbrain::logging
