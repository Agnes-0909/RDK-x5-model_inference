#ifndef MIPI_VSE_BPU_ZEROCOPY_LOG_EVENT_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_LOG_EVENT_HPP_

#include "app_config.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace zerocopy {

using LogValue =
    std::variant<std::string, int64_t, uint64_t, double, bool>;
using LogFields = std::map<std::string, LogValue>;

struct LogEvent {
  LogLevel level = LogLevel::kInfo;
  std::string component;
  std::string event;
  std::string thread;
  std::string channel;
  int64_t frame_id = -1;
  std::string message;
  LogFields fields;

  uint64_t sequence = 0;
  uint64_t monotonic_us = 0;
  std::string wall_time;
};

const char* logLevelName(LogLevel level) noexcept;

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_LOG_EVENT_HPP_
