#pragma once

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

namespace rcpputils {
namespace fs = std::filesystem;
}  // namespace rcpputils

namespace rclcpp {

class Logger {
 public:
  explicit Logger(const char* name) : name_(name == nullptr ? "rclcpp" : name) {}
  const std::string& name() const { return name_; }

 private:
  std::string name_;
};

inline Logger get_logger(const char* name) { return Logger(name); }
inline Logger get_logger(const std::string& name) { return Logger(name.c_str()); }

inline void init(int, char**) {}
inline void shutdown() {}
inline bool ok() { return true; }

}  // namespace rclcpp

#define RCLCPP_LOG_PRINTF(level, logger, fmt, ...)                  \
  do {                                                             \
    std::fprintf(stderr, "[%s] [%s] " fmt, level,                  \
                 (logger).name().c_str(), ##__VA_ARGS__);          \
  } while (0)

#define RCLCPP_INFO(logger, fmt, ...) \
  RCLCPP_LOG_PRINTF("INFO", logger, fmt, ##__VA_ARGS__)
#define RCLCPP_WARN(logger, fmt, ...) \
  RCLCPP_LOG_PRINTF("WARN", logger, fmt, ##__VA_ARGS__)
#define RCLCPP_ERROR(logger, fmt, ...) \
  RCLCPP_LOG_PRINTF("ERROR", logger, fmt, ##__VA_ARGS__)

#define RCLCPP_INFO_STREAM(logger, expr)                                      \
  do {                                                                        \
    std::cerr << "[INFO] [" << (logger).name() << "] " << expr << std::endl; \
  } while (0)
#define RCLCPP_WARN_STREAM(logger, expr)                                      \
  do {                                                                        \
    std::cerr << "[WARN] [" << (logger).name() << "] " << expr << std::endl; \
  } while (0)
#define RCLCPP_ERROR_STREAM(logger, expr)                                      \
  do {                                                                         \
    std::cerr << "[ERROR] [" << (logger).name() << "] " << expr << std::endl; \
  } while (0)
