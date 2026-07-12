#include "bounded_frame_queue.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
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

zerocopy::BufferLayout layout() {
  zerocopy::BufferLayout value;
  value.width = value.height = value.stride_y = value.stride_uv = 640;
  value.y_bytes = 409600;
  value.uv_bytes = 204800;
  value.y_phy = 0x100000;
  value.uv_phy = 0x164000;
  value.y_virt = reinterpret_cast<void*>(0x200000);
  value.uv_virt = reinterpret_cast<void*>(0x264000);
  value.plane_count = 2;
  value.format = zerocopy::PixelFormat::Nv12;
  return value;
}

zerocopy::VseFrame frame(uint64_t id, std::atomic<int>& releases,
                         uint64_t hardware_timestamp = 0) {
  return zerocopy::VseFrame(id, layout(), [&releases] {
    ++releases;
    return 0;
  }, hardware_timestamp);
}

void testMoveOwnershipAndFifo() {
  std::atomic<int> releases{0};
  zerocopy::BoundedFrameQueue queue;
  auto input = frame(7, releases, 123456);
  const auto pushed = queue.push(std::move(input));
  CHECK(pushed.accepted);
  CHECK(!input.valid());
  CHECK(queue.size() == 1);

  zerocopy::VseFrame output;
  const auto popped = queue.pop(output);
  CHECK(popped.has_frame);
  CHECK(output.frameId() == 7);
  CHECK(output.hardwareTimestamp() == 123456);
  CHECK(queue.size() == 0);
  output.release();
  CHECK(releases == 1);
}

void testFullQueueBlocksAndPopWakesProducer() {
  using namespace std::chrono_literals;
  std::atomic<int> releases{0};
  zerocopy::BoundedFrameQueue queue;
  auto first = frame(1, releases);
  CHECK(queue.push(std::move(first)).accepted);

  std::promise<void> entered;
  std::atomic<bool> finished{false};
  zerocopy::QueuePushResult second_result;
  std::thread producer([&] {
    auto second = frame(2, releases);
    entered.set_value();
    second_result = queue.push(std::move(second));
    finished = true;
  });
  entered.get_future().wait();
  std::this_thread::sleep_for(40ms);
  CHECK(!finished.load());

  zerocopy::VseFrame output;
  CHECK(queue.pop(output).has_frame);
  CHECK(output.frameId() == 1);
  output.release();
  producer.join();
  CHECK(second_result.accepted);
  CHECK(second_result.blocked.count() >= 20'000);
  CHECK(queue.pop(output).has_frame);
  CHECK(output.frameId() == 2);
  output.release();
  CHECK(releases == 2);
}

void testCloseWakesPushAndPopAndDrainsQueuedFrame() {
  using namespace std::chrono_literals;
  std::atomic<int> releases{0};
  zerocopy::BoundedFrameQueue queue;
  auto first = frame(10, releases);
  CHECK(queue.push(std::move(first)).accepted);

  std::promise<void> push_entered;
  zerocopy::QueuePushResult push_result;
  std::thread producer([&] {
    auto second = frame(11, releases);
    push_entered.set_value();
    push_result = queue.push(std::move(second));
  });
  push_entered.get_future().wait();
  std::this_thread::sleep_for(20ms);
  queue.close();
  producer.join();
  CHECK(!push_result.accepted);
  CHECK(releases == 1);

  zerocopy::VseFrame output;
  CHECK(queue.pop(output).has_frame);
  CHECK(output.frameId() == 10);
  output.release();
  CHECK(!queue.pop(output).has_frame);
  CHECK(releases == 2);

  zerocopy::BoundedFrameQueue empty_queue;
  std::promise<void> pop_entered;
  zerocopy::QueuePopResult pop_result;
  std::thread consumer([&] {
    zerocopy::VseFrame unused;
    pop_entered.set_value();
    pop_result = empty_queue.pop(unused);
  });
  pop_entered.get_future().wait();
  std::this_thread::sleep_for(20ms);
  empty_queue.close();
  consumer.join();
  CHECK(!pop_result.has_frame);
}

void testDestructorReleasesQueuedFrameOnce() {
  std::atomic<int> releases{0};
  {
    zerocopy::BoundedFrameQueue queue;
    auto input = frame(99, releases);
    CHECK(queue.push(std::move(input)).accepted);
  }
  CHECK(releases == 1);
}

}  // namespace

int main() {
  testMoveOwnershipAndFifo();
  testFullQueueBlocksAndPopWakesProducer();
  testCloseWakesPushAndPopAndDrainsQueuedFrame();
  testDestructorReleasesQueuedFrameOnce();
  if (failures != 0) return 1;
  std::cout << "test_bounded_frame_queue: PASS\n";
  return 0;
}
