#include "channel_worker.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                          \
      std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: "           \
                << #condition << '\n';                                           \
      ++failures;                                                                \
    }                                                                            \
  } while (false)

zerocopy::BufferLayout layout(uint64_t offset) {
  zerocopy::BufferLayout value;
  value.width = value.height = value.stride_y = value.stride_uv = 640;
  value.y_bytes = 409600;
  value.uv_bytes = 204800;
  value.y_phy = 0x100000 + offset;
  value.uv_phy = value.y_phy + value.y_bytes;
  value.y_virt = reinterpret_cast<void*>(0x200000 + offset);
  value.uv_virt = reinterpret_cast<void*>(0x264000 + offset);
  value.plane_count = 2;
  value.format = zerocopy::PixelFormat::Nv12;
  return value;
}

zerocopy::VseFrame frame(uint64_t id, std::atomic<int>& releases) {
  return zerocopy::VseFrame(id, layout(id * 0x100000), [&releases] {
    ++releases;
    return 0;
  });
}

class TestProcessor final : public zerocopy::ChannelProcessor {
 public:
  TestProcessor(int identity, std::atomic<int>* active = nullptr,
                std::atomic<int>* maximum = nullptr, uint64_t fail_frame = 0)
      : identity_(identity),
        active_(active),
        maximum_(maximum),
        fail_frame_(fail_frame) {}

  bool initialize(std::string&) override {
    initialized_ = true;
    return true;
  }

  bool process(zerocopy::VseFrame& frame,
               zerocopy::ChannelProcessResult& result,
               std::string& error) override {
    if (!initialized_) {
      error = "not initialized";
      return false;
    }
    observed_identity = identity_;
    if (release_counter != nullptr && release_counter->load() != 0) {
      released_during_process = true;
    }
    if (active_ != nullptr) {
      const int now = ++(*active_);
      int previous = maximum_->load();
      while (now > previous &&
             !maximum_->compare_exchange_weak(previous, now)) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(60));
      --(*active_);
    }
    if (frame.frameId() == fail_frame_) {
      error = "processor failure";
      return false;
    }
    result.detections = static_cast<std::size_t>(identity_);
    result.cache_hit = frame.frameId() > 1;
    result.address_match = true;
    result.bpu_ms = identity_ * 1.0;
    result.postprocess_ms = identity_ * 0.5;
    return true;
  }

  int shutdown() noexcept override {
    shutdown_called = true;
    return 0;
  }

  std::atomic<int>* release_counter = nullptr;
  bool released_during_process = false;
  bool shutdown_called = false;
  int observed_identity = 0;

 private:
  int identity_;
  std::atomic<int>* active_;
  std::atomic<int>* maximum_;
  uint64_t fail_frame_;
  bool initialized_ = false;
};

void testIndependentProcessorsRunConcurrentlyAndReleaseAfterProcess() {
  std::atomic<int> active{0};
  std::atomic<int> maximum{0};
  std::atomic<int> left_releases{0};
  std::atomic<int> right_releases{0};
  auto left_processor = std::make_unique<TestProcessor>(1, &active, &maximum);
  auto right_processor = std::make_unique<TestProcessor>(2, &active, &maximum);
  auto* left_raw = left_processor.get();
  auto* right_raw = right_processor.get();
  left_raw->release_counter = &left_releases;
  right_raw->release_counter = &right_releases;

  zerocopy::ChannelWorker left("left", std::move(left_processor));
  zerocopy::ChannelWorker right("right", std::move(right_processor));
  CHECK(left.initialize());
  CHECK(right.initialize());
  CHECK(left.enqueue(frame(1, left_releases)).accepted);
  CHECK(right.enqueue(frame(1, right_releases)).accepted);
  left.start();
  right.start();
  left.close();
  right.close();
  left.join();
  right.join();

  CHECK(maximum.load() == 2);
  CHECK(left_raw->observed_identity == 1);
  CHECK(right_raw->observed_identity == 2);
  CHECK(!left_raw->released_during_process);
  CHECK(!right_raw->released_during_process);
  CHECK(left_releases == 1);
  CHECK(right_releases == 1);
  const auto left_stats = left.stats();
  const auto right_stats = right.stats();
  CHECK(left_stats.successes == 1);
  CHECK(right_stats.successes == 1);
  CHECK(left_stats.detections == 1);
  CHECK(right_stats.detections == 2);
  CHECK(left_stats.bpu_ms == 1.0);
  CHECK(right_stats.bpu_ms == 2.0);
  CHECK(left.shutdownProcessor() == 0);
  CHECK(right.shutdownProcessor() == 0);
  CHECK(left_raw->shutdown_called);
  CHECK(right_raw->shutdown_called);
}

void testFirstErrorPropagatesAndWorkerJoinsCleanly() {
  std::atomic<int> releases{0};
  int fatal_calls = 0;
  std::string fatal_channel;
  uint64_t fatal_frame = 0;
  std::string fatal_error;
  auto processor = std::make_unique<TestProcessor>(3, nullptr, nullptr, 2);
  zerocopy::ChannelWorker worker(
      "left", std::move(processor), nullptr,
      [&](const std::string& channel, uint64_t frame_id,
          const std::string& error) {
        ++fatal_calls;
        fatal_channel = channel;
        fatal_frame = frame_id;
        fatal_error = error;
      });
  CHECK(worker.initialize());
  worker.start();
  CHECK(worker.enqueue(frame(2, releases)).accepted);
  worker.close();
  worker.join();
  CHECK(fatal_calls == 1);
  CHECK(fatal_channel == "left");
  CHECK(fatal_frame == 2);
  CHECK(fatal_error == "processor failure");
  CHECK(worker.lastError() == "processor failure");
  CHECK(worker.stats().failures == 1);
  CHECK(releases == 1);
  CHECK(worker.shutdownProcessor() == 0);
}

}  // namespace

int main() {
  testIndependentProcessorsRunConcurrentlyAndReleaseAfterProcess();
  testFirstErrorPropagatesAndWorkerJoinsCleanly();
  if (failures != 0) return 1;
  std::cout << "test_channel_worker: PASS\n";
  return 0;
}
