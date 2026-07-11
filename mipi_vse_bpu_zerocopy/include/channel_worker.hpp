#ifndef MIPI_VSE_BPU_ZEROCOPY_CHANNEL_WORKER_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_CHANNEL_WORKER_HPP_

#include "async_logger.hpp"
#include "bounded_frame_queue.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace zerocopy {

struct ChannelProcessResult {
  std::size_t detections = 0;
  bool cache_hit = false;
  bool address_match = true;
  double bpu_ms = 0.0;
  double postprocess_ms = 0.0;
  double end_to_end_ms = 0.0;
};

class ChannelProcessor {
 public:
  virtual ~ChannelProcessor() = default;
  virtual bool initialize(std::string& error) = 0;
  virtual bool process(VseFrame& frame, ChannelProcessResult& result,
                       std::string& error) = 0;
  virtual int shutdown() noexcept = 0;
};

struct ChannelStats {
  uint64_t successes = 0;
  uint64_t failures = 0;
  uint64_t detections = 0;
  uint64_t cache_hits = 0;
  uint64_t cache_misses = 0;
  uint64_t address_mismatches = 0;
  uint64_t release_errors = 0;
  double bpu_ms = 0.0;
  double postprocess_ms = 0.0;
  double end_to_end_ms = 0.0;
  double queue_push_wait_ms = 0.0;
  double queue_pop_wait_ms = 0.0;
};

class ChannelWorker {
 public:
  using FatalCallback = std::function<void(
      const std::string&, uint64_t, const std::string&)>;

  ChannelWorker(std::string channel,
                std::unique_ptr<ChannelProcessor> processor,
                AsyncLogger* logger = nullptr,
                FatalCallback fatal_callback = {});
  ~ChannelWorker();

  ChannelWorker(const ChannelWorker&) = delete;
  ChannelWorker& operator=(const ChannelWorker&) = delete;

  bool initialize();
  void start();
  QueuePushResult enqueue(VseFrame&& frame);
  void close() noexcept;
  void join() noexcept;
  int shutdownProcessor() noexcept;

  ChannelStats stats() const;
  std::string lastError() const;
  const std::string& channel() const noexcept;

 private:
  void run() noexcept;
  void recordFailure(uint64_t frame_id, const std::string& error);
  void emit(LogLevel level, const std::string& event, int64_t frame_id,
            const std::string& message, LogFields fields = {}) noexcept;

  std::string channel_;
  std::unique_ptr<ChannelProcessor> processor_;
  AsyncLogger* logger_;
  FatalCallback fatal_callback_;
  BoundedFrameQueue queue_;
  mutable std::mutex state_mutex_;
  ChannelStats stats_;
  std::string last_error_;
  bool initialized_ = false;
  bool started_ = false;
  bool processor_shutdown_ = false;
  std::atomic_bool closed_{false};
  std::thread thread_;
};

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_CHANNEL_WORKER_HPP_
