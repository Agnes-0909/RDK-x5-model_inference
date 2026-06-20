#include "logger.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace mt {

std::string logTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

  std::tm local_tm{};
#if defined(_WIN32)
  localtime_s(&local_tm, &now_time);
#else
  localtime_r(&now_time, &local_tm);
#endif

  std::ostringstream oss;
  oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S")
      << "." << std::setw(3) << std::setfill('0') << ms.count();
  return oss.str();
}

const char* logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
  }
  return "INFO";
}

std::string shortFileName(const char* file) {
  if (file == nullptr) {
    return {};
  }

  const std::string path(file);
  const std::size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

void logMessage(LogLevel level, const char* file, const char* function,
                const std::string& message) {
  std::ostream& os =
      level == LogLevel::kError ? std::cerr : std::cout;
  os << "[" << logTimestamp() << "] "
     << "[" << logLevelName(level) << "] "
     << "[" << shortFileName(file) << "-" << function << "] "
     << message << "\n";
}

int64_t elapsedMs(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
      .count();
}

}  // namespace mt
