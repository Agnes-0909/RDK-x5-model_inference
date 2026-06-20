#pragma once

#include <chrono>
#include <string>

namespace mt {

enum class LogLevel {
  kInfo,
  kWarn,
  kError,
};

std::string logTimestamp();
const char* logLevelName(LogLevel level);
std::string shortFileName(const char* file);
void logMessage(LogLevel level, const char* file, const char* function,
                const std::string& message);
int64_t elapsedMs(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end);

}  // namespace mt

#define MT_LOG_INFO(message) \
  ::mt::logMessage(::mt::LogLevel::kInfo, __FILE__, __func__, (message))

#define MT_LOG_WARN(message) \
  ::mt::logMessage(::mt::LogLevel::kWarn, __FILE__, __func__, (message))

#define MT_LOG_ERROR(message) \
  ::mt::logMessage(::mt::LogLevel::kError, __FILE__, __func__, (message))
