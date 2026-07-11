#ifndef MIPI_VSE_BPU_ZEROCOPY_DUAL_CHANNEL_COORDINATOR_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_DUAL_CHANNEL_COORDINATOR_HPP_

#include "channel_worker.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace zerocopy {

struct DualChannelSummary {
  ChannelStats left;
  ChannelStats right;
};

class DualChannelCoordinator {
 public:
  DualChannelCoordinator(std::unique_ptr<ChannelProcessor> left_processor,
                         std::unique_ptr<ChannelProcessor> right_processor,
                         AsyncLogger* logger = nullptr);
  ~DualChannelCoordinator();

  bool initialize();
  void start();
  bool enqueuePair(VseFrame&& left, VseFrame&& right);
  void close() noexcept;
  void join() noexcept;
  int shutdownProcessors() noexcept;

  bool stopRequested() const noexcept;
  std::string lastError() const;
  DualChannelSummary summary() const;

 private:
  void fatal(const std::string& channel, uint64_t frame_id,
             const std::string& error);

  std::atomic_bool stop_requested_{false};
  mutable std::mutex error_mutex_;
  std::string last_error_;
  ChannelWorker left_;
  ChannelWorker right_;
};

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_DUAL_CHANNEL_COORDINATOR_HPP_
