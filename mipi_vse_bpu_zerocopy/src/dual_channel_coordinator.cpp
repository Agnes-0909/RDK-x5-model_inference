#include "dual_channel_coordinator.hpp"

#include <utility>

namespace zerocopy {

DualChannelCoordinator::DualChannelCoordinator(
    std::unique_ptr<ChannelProcessor> left_processor,
    std::unique_ptr<ChannelProcessor> right_processor, AsyncLogger* logger)
    : left_("left", std::move(left_processor), logger,
            [this](const std::string& channel, uint64_t frame_id,
                   const std::string& error) {
              fatal(channel, frame_id, error);
            }),
      right_("right", std::move(right_processor), logger,
             [this](const std::string& channel, uint64_t frame_id,
                    const std::string& error) {
               fatal(channel, frame_id, error);
             }) {}

DualChannelCoordinator::~DualChannelCoordinator() {
  close();
  join();
  shutdownProcessors();
}

bool DualChannelCoordinator::initialize() {
  if (!left_.initialize()) {
    fatal("left", 0, left_.lastError());
    return false;
  }
  if (!right_.initialize()) {
    fatal("right", 0, right_.lastError());
    return false;
  }
  return true;
}

void DualChannelCoordinator::start() {
  left_.start();
  right_.start();
}

bool DualChannelCoordinator::enqueuePair(VseFrame&& left, VseFrame&& right) {
  if (stop_requested_.load()) return false;
  const auto left_result = left_.enqueue(std::move(left));
  if (!left_result.accepted || stop_requested_.load()) return false;
  const auto right_result = right_.enqueue(std::move(right));
  return right_result.accepted && !stop_requested_.load();
}

void DualChannelCoordinator::close() noexcept {
  left_.close();
  right_.close();
}

void DualChannelCoordinator::join() noexcept {
  left_.join();
  right_.join();
}

int DualChannelCoordinator::shutdownProcessors() noexcept {
  const int left_result = left_.shutdownProcessor();
  const int right_result = right_.shutdownProcessor();
  return left_result != 0 ? left_result : right_result;
}

bool DualChannelCoordinator::stopRequested() const noexcept {
  return stop_requested_.load();
}

std::string DualChannelCoordinator::lastError() const {
  std::lock_guard<std::mutex> lock(error_mutex_);
  return last_error_;
}

DualChannelSummary DualChannelCoordinator::summary() const {
  return {left_.stats(), right_.stats()};
}

void DualChannelCoordinator::fatal(const std::string& channel,
                                   uint64_t frame_id,
                                   const std::string& error) {
  bool expected = false;
  if (stop_requested_.compare_exchange_strong(expected, true)) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = channel + " frame=" + std::to_string(frame_id) + ": " + error;
  }
  close();
}

}  // namespace zerocopy
