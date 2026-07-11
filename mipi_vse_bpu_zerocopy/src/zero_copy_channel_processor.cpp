#include "zero_copy_channel_processor.hpp"

#include "registered_buffer_cache.hpp"

#include <utility>

#include "dnn/hb_sys.h"

namespace zerocopy {
namespace {

int registerMemory(ExternalMemory& memory) {
  hbSysMem descriptor{};
  descriptor.phyAddr = memory.phy_addr;
  descriptor.virAddr = memory.virt_addr;
  descriptor.memSize = memory.bytes;
  const int result = hbSysRegisterMem(&descriptor);
  if (result == 0) {
    memory = {descriptor.phyAddr, descriptor.virAddr, descriptor.memSize,
              descriptor.memSize};
  }
  return result;
}

int unregisterMemory(ExternalMemory& memory) {
  hbSysMem descriptor{};
  descriptor.phyAddr = memory.phy_addr;
  descriptor.virAddr = memory.virt_addr;
  descriptor.memSize = memory.bytes;
  return hbSysUnregisterMem(&descriptor);
}

class ZeroCopyChannelProcessor final : public ChannelProcessor {
 public:
  ZeroCopyChannelProcessor(std::string channel,
                           ZeroCopyYoloRunner::Config config,
                           AsyncLogger* logger)
      : channel_(std::move(channel)),
        runner_(std::move(config)),
        cache_(registerMemory, unregisterMemory),
        logger_(logger) {}

  bool initialize(std::string& error) override {
    if (!runner_.initialize()) {
      error = runner_.lastError();
      return false;
    }
    emit(LogLevel::kInfo, "dnn", "model_loaded", 0,
         "independent model instance initialized",
         {{"bpu_core", int64_t{kX5BpuCore0}}});
    return true;
  }

  bool process(VseFrame& frame, ChannelProcessResult& result,
               std::string& error) override {
    RegisteredBuffer registered;
    try {
      registered = cache_.getOrRegister(frame.layout());
    } catch (const std::exception& exception) {
      error = exception.what();
      return false;
    }
    emit(LogLevel::kDebug, "memory",
         registered.cache_hit ? "registration_cache_hit" : "memory_registered",
         frame.frameId(), "VSE buffer is registered directly with DNN",
         {{"bytes", uint64_t{registered.memory.bytes}},
          {"cache_hit", registered.cache_hit},
          {"physical_address", registered.memory.phy_addr}});

    InferenceResult inference;
    if (!runner_.infer(registered.memory, inference)) {
      error = runner_.lastError();
      return false;
    }
    result.detections = inference.detections.size();
    result.cache_hit = registered.cache_hit;
    result.address_match = inference.dnn_input_phy == frame.layout().y_phy;
    result.bpu_ms = inference.bpu_ms;
    result.postprocess_ms = inference.postprocess_ms;
    emit(LogLevel::kInfo, "dnn", "inference_complete", frame.frameId(),
         "BPU inference completed with VSE address",
         {{"address_match", result.address_match},
          {"bpu_core", int64_t{inference.bpu.core_id}},
          {"dnn_phy", inference.dnn_input_phy},
          {"invalidate_ms", inference.bpu.invalidate_ms},
          {"release_ms", inference.bpu.task_release_ms},
          {"submit_ms", inference.bpu.submit_ms},
          {"total_ms", inference.bpu.total_ms},
          {"vse_phy", frame.layout().y_phy},
          {"wait_ms", inference.bpu.wait_ms}});
    emit(LogLevel::kDebug, "postprocess", "postprocess_complete",
         frame.frameId(), "YOLO11 CPU postprocess completed",
         {{"class_scan_ms", inference.postprocess.class_scan_ms},
          {"detections", uint64_t{inference.postprocess.detections}},
          {"dfl_decode_ms", inference.postprocess.dfl_decode_ms},
          {"map_ms", inference.postprocess.map_ms},
          {"nms_ms", inference.postprocess.nms_ms},
          {"proposals", uint64_t{inference.postprocess.proposals}},
          {"total_ms", inference.postprocess.total_ms}});
    if (!result.address_match) {
      error = "zero-copy proof failed: VSE and DNN physical addresses differ";
      return false;
    }
    return true;
  }

  int shutdown() noexcept override {
    const std::size_t count = cache_.size();
    const int result = cache_.clear();
    emit(result == 0 ? LogLevel::kInfo : LogLevel::kError, "memory",
         "registration_cache_cleared", 0, "registered buffers unregistered",
         {{"buffers", static_cast<uint64_t>(count)},
          {"result", static_cast<int64_t>(result)}});
    return result;
  }

 private:
  void emit(LogLevel level, const std::string& component,
            const std::string& event, uint64_t frame_id,
            const std::string& message, LogFields fields) noexcept {
    if (logger_ == nullptr) return;
    try {
      logger_->log({level, component, event, channel_ + "-worker", channel_,
                    frame_id == 0 ? -1 : static_cast<int64_t>(frame_id),
                    message, std::move(fields)});
    } catch (...) {
    }
  }

  std::string channel_;
  ZeroCopyYoloRunner runner_;
  RegisteredBufferCache cache_;
  AsyncLogger* logger_;
};

}  // namespace

std::unique_ptr<ChannelProcessor> makeZeroCopyChannelProcessor(
    std::string channel, ZeroCopyYoloRunner::Config config,
    AsyncLogger* logger) {
  return std::make_unique<ZeroCopyChannelProcessor>(
      std::move(channel), std::move(config), logger);
}

}  // namespace zerocopy
