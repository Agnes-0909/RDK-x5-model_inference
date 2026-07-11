#ifndef MIPI_VSE_BPU_ZEROCOPY_ASYNC_LOGGER_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_ASYNC_LOGGER_HPP_

#include "log_event.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace zerocopy {

struct LoggerConfig {
  std::string directory = "logs";
  LogLevel min_level = LogLevel::kInfo;
  uint64_t max_bytes = 100ULL * 1024ULL * 1024ULL;
  int max_files = 5;
  std::size_t queue_capacity = 4096;
  bool console = true;
};

class AsyncLogger {
 public:
  explicit AsyncLogger(LoggerConfig config);
  ~AsyncLogger();

  AsyncLogger(const AsyncLogger&) = delete;
  AsyncLogger& operator=(const AsyncLogger&) = delete;

  void start();
  void log(LogEvent event);
  void shutdown() noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_ASYNC_LOGGER_HPP_
