#include "dual_channel_coordinator.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

zerocopy::BufferLayout layout(uint64_t id) {
  zerocopy::BufferLayout value;
  value.width = value.height = value.stride_y = value.stride_uv = 640;
  value.y_bytes = 409600;
  value.uv_bytes = 204800;
  value.y_phy = 0x100000 + id * 0x100000;
  value.uv_phy = value.y_phy + value.y_bytes;
  value.y_virt = reinterpret_cast<void*>(0x200000 + id * 0x100000);
  value.uv_virt = reinterpret_cast<void*>(0x264000 + id * 0x100000);
  value.plane_count = 2;
  value.format = zerocopy::PixelFormat::Nv12;
  return value;
}

zerocopy::VseFrame frame(uint64_t id, std::atomic<int>& releases) {
  return {id, layout(id), [&releases] { ++releases; return 0; }};
}

class SlowProcessor final : public zerocopy::ChannelProcessor {
 public:
  SlowProcessor(std::string name, std::vector<std::string>& order)
      : name_(std::move(name)), order_(order) {}
  bool initialize(std::string&) override {
    order_.push_back(name_);
    return true;
  }
  bool process(zerocopy::VseFrame&, zerocopy::ChannelProcessResult& result,
               std::string&) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    result.address_match = true;
    return true;
  }
  int shutdown() noexcept override { return 0; }
 private:
  std::string name_;
  std::vector<std::string>& order_;
};
}  // namespace

int main() {
  std::vector<std::string> initialization_order;
  std::atomic<int> left_releases{0};
  std::atomic<int> right_releases{0};
  zerocopy::DualChannelCoordinator coordinator(
      std::make_unique<SlowProcessor>("left", initialization_order),
      std::make_unique<SlowProcessor>("right", initialization_order));
  CHECK(coordinator.initialize());
  CHECK((initialization_order == std::vector<std::string>{"left", "right"}));
  coordinator.start();
  const auto start = std::chrono::steady_clock::now();
  for (uint64_t id = 1; id <= 3; ++id) {
    auto left = frame(id, left_releases);
    auto right = frame(id + 10, right_releases);
    CHECK(coordinator.enqueuePair(std::move(left), std::move(right)));
  }
  const auto blocked_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
  CHECK(blocked_ms >= 35.0);
  coordinator.close();
  coordinator.join();
  const auto summary = coordinator.summary();
  CHECK(summary.left.successes == 3);
  CHECK(summary.right.successes == 3);
  CHECK(summary.left.failures == 0);
  CHECK(summary.right.failures == 0);
  CHECK(left_releases == 3);
  CHECK(right_releases == 3);
  CHECK(!coordinator.stopRequested());
  CHECK(coordinator.shutdownProcessors() == 0);
  if (failures != 0) return 1;
  std::cout << "test_dual_channel_coordinator: PASS\n";
  return 0;
}
