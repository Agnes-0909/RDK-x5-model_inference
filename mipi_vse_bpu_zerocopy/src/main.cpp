#include "app_config.hpp"
#include "registered_buffer_cache.hpp"
#include "vio_pipeline.hpp"
#include "zero_copy_yolo_runner.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include "dnn/hb_sys.h"
#include "hb_mem_mgr.h"

namespace {

std::atomic_bool stop_requested{false};

void handleSignal(int) { stop_requested.store(true); }

int registerMemory(zerocopy::ExternalMemory& memory) {
  hbSysMem descriptor{};
  descriptor.phyAddr = memory.phy_addr;
  descriptor.virAddr = memory.virt_addr;
  descriptor.memSize = memory.bytes;
  const int ret = hbSysRegisterMem(&descriptor);
  if (ret == 0) {
    memory.phy_addr = descriptor.phyAddr;
    memory.virt_addr = descriptor.virAddr;
    memory.bytes = descriptor.memSize;
    memory.total_bytes = descriptor.memSize;
  }
  return ret;
}

int unregisterMemory(zerocopy::ExternalMemory& memory) {
  hbSysMem descriptor{};
  descriptor.phyAddr = memory.phy_addr;
  descriptor.virAddr = memory.virt_addr;
  descriptor.memSize = memory.bytes;
  return hbSysUnregisterMem(&descriptor);
}

struct ChannelStats {
  int successes = 0;
  int timeouts = 0;
  std::size_t detections = 0;
  double bpu_ms = 0.0;
  double postprocess_ms = 0.0;
};

void printResult(const char* channel, const zerocopy::VseFrame& frame,
                 const zerocopy::RegisteredBuffer& registered,
                 const zerocopy::InferenceResult& result) {
  std::cout << "frame channel=" << channel << " id=" << frame.frameId()
            << " vse_phy=0x" << std::hex << frame.layout().y_phy
            << " dnn_phy=0x" << result.dnn_input_phy << std::dec
            << " cache_hit=" << (registered.cache_hit ? 1 : 0)
            << " bpu_ms=" << std::fixed << std::setprecision(3)
            << result.bpu_ms << " postprocess_ms=" << result.postprocess_ms
            << " detections=" << result.detections.size() << '\n';
  for (const auto& detection : result.detections) {
    std::cout << "  detection channel=" << channel
              << " class=" << detection.class_id
              << " score=" << detection.score << " box=[" << detection.box.x1
              << ',' << detection.box.y1 << ',' << detection.box.x2 << ','
              << detection.box.y2 << "]\n";
  }
}

bool inferFrame(const char* channel, zerocopy::VseFrame& frame,
                zerocopy::RegisteredBufferCache& cache,
                zerocopy::ZeroCopyYoloRunner& runner, ChannelStats& stats) {
  const auto registered = cache.getOrRegister(frame.layout());
  zerocopy::InferenceResult result;
  if (!runner.infer(registered.memory, result)) {
    std::cerr << "BPU inference failed channel=" << channel
              << " frame_id=" << frame.frameId()
              << " error=" << runner.lastError() << '\n';
    return false;
  }
  if (result.dnn_input_phy != frame.layout().y_phy) {
    throw std::runtime_error(
        "zero-copy proof failed: VSE and DNN physical addresses differ");
  }
  printResult(channel, frame, registered, result);
  ++stats.successes;
  stats.detections += result.detections.size();
  stats.bpu_ms += result.bpu_ms;
  stats.postprocess_ms += result.postprocess_ms;
  const int release_ret = frame.release();
  if (release_ret != 0) {
    throw std::runtime_error("hbn_vnode_releaseframe failed with code " +
                             std::to_string(release_ret));
  }
  return true;
}

void printSummary(const ChannelStats& left, const ChannelStats& right,
                  std::size_t registered_buffers) {
  auto average = [](double total, int count) {
    return count == 0 ? 0.0 : total / static_cast<double>(count);
  };
  std::cout << "summary left_success=" << left.successes
            << " right_success=" << right.successes
            << " left_timeouts=" << left.timeouts
            << " right_timeouts=" << right.timeouts
            << " left_detections=" << left.detections
            << " right_detections=" << right.detections
            << " left_avg_bpu_ms=" << average(left.bpu_ms, left.successes)
            << " right_avg_bpu_ms=" << average(right.bpu_ms, right.successes)
            << " left_avg_post_ms="
            << average(left.postprocess_ms, left.successes)
            << " right_avg_post_ms="
            << average(right.postprocess_ms, right.successes)
            << " registered_buffers=" << registered_buffers << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  zerocopy::AppConfig config;
  try {
    config = zerocopy::parseArgs(argc, argv);
  } catch (const std::invalid_argument& error) {
    if (std::string(error.what()) == "help requested") {
      std::cout << zerocopy::usage(argv[0]) << '\n';
      return 0;
    }
    std::cerr << error.what() << '\n' << zerocopy::usage(argv[0]) << '\n';
    return 2;
  }

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);
  const int mem_ret = hb_mem_module_open();
  if (mem_ret != 0) {
    std::cerr << "hb_mem_module_open failed with code " << mem_ret << '\n';
    return 1;
  }

  int exit_code = 0;
  ChannelStats left_stats;
  ChannelStats right_stats;
  std::size_t registered_count = 0;
  {
    zerocopy::ZeroCopyYoloRunner runner(
        {config.model_path, config.dnn_timeout_ms, config.score_threshold,
         config.nms_threshold});
    zerocopy::DualVioPipeline pipelines(config.left_host, config.right_host);
    zerocopy::RegisteredBufferCache cache(registerMemory, unregisterMemory);
    try {
      if (!runner.initialize()) {
        throw std::runtime_error("DNN initialization failed: " +
                                 runner.lastError());
      }
      if (!pipelines.initialize()) {
        throw std::runtime_error("VIO initialization failed: " +
                                 pipelines.lastError());
      }

      int consecutive_timeouts = 0;
      while (!stop_requested.load() &&
             (config.frames == 0 || left_stats.successes < config.frames ||
              right_stats.successes < config.frames)) {
        zerocopy::VseFrame left_frame;
        zerocopy::VseFrame right_frame;
        bool left_ok = left_stats.successes >= config.frames && config.frames > 0;
        bool right_ok =
            right_stats.successes >= config.frames && config.frames > 0;
        if (!left_ok) {
          left_ok = pipelines.acquireLeft(config.frame_timeout_ms, left_frame);
          if (!left_ok) ++left_stats.timeouts;
        }
        if (!right_ok) {
          right_ok = pipelines.acquireRight(config.frame_timeout_ms, right_frame);
          if (!right_ok) ++right_stats.timeouts;
        }
        if (!left_ok || !right_ok) {
          ++consecutive_timeouts;
          std::cerr << "frame acquisition failed: " << pipelines.lastError()
                    << " consecutive=" << consecutive_timeouts << '\n';
          if (consecutive_timeouts >= config.max_consecutive_timeouts) {
            throw std::runtime_error("too many consecutive frame timeouts");
          }
          continue;
        }
        consecutive_timeouts = 0;
        if (left_frame.valid() &&
            !inferFrame("left", left_frame, cache, runner, left_stats)) {
          throw std::runtime_error("left inference failed");
        }
        if (right_frame.valid() &&
            !inferFrame("right", right_frame, cache, runner, right_stats)) {
          throw std::runtime_error("right inference failed");
        }
      }
      registered_count = cache.size();
      printSummary(left_stats, right_stats, registered_count);
      const int unregister_ret = cache.clear();
      if (unregister_ret != 0) {
        throw std::runtime_error("hbSysUnregisterMem failed with code " +
                                 std::to_string(unregister_ret));
      }
      pipelines.stop();
    } catch (const std::exception& error) {
      registered_count = cache.size();
      std::cerr << "fatal: " << error.what() << '\n';
      printSummary(left_stats, right_stats, registered_count);
      cache.clear();
      pipelines.stop();
      exit_code = 1;
    }
  }
  hb_mem_module_close();
  return exit_code;
}
