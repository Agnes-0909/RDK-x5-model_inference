#include "zero_copy_yolo_runner.hpp"

#include "yolo11_postprocess.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <sstream>
#include <utility>

#include "dnn/hb_dnn.h"
#include "dnn/hb_sys.h"

namespace zerocopy {
namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(const Clock::time_point& start,
                 const Clock::time_point& end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

bool shapeMatches(const hbDNNTensorShape& shape, int n, int h, int w, int c) {
  return shape.numDimensions == 4 && shape.dimensionSize[0] == n &&
         shape.dimensionSize[1] == h && shape.dimensionSize[2] == w &&
         shape.dimensionSize[3] == c;
}

}  // namespace

class ZeroCopyYoloRunner::Impl {
 public:
  explicit Impl(Config config)
      : config_(std::move(config)),
        postprocessor_(config_.score_threshold, config_.nms_threshold) {}

  ~Impl() { release(); }

  bool initialize() {
    if (initialized_) return true;
    if (config_.model_path.empty()) return fail("model path is empty");
    if (config_.bpu_core_id != kX5BpuCore0) {
      return fail("this X5 application supports only BPU core 0");
    }

    const char* model_file = config_.model_path.c_str();
    int32_t ret = hbDNNInitializeFromFiles(&packed_, &model_file, 1);
    if (ret != 0) return failCode("hbDNNInitializeFromFiles", ret);

    const char** names = nullptr;
    int32_t model_count = 0;
    ret = hbDNNGetModelNameList(&names, &model_count, packed_);
    if (ret != 0) return failCode("hbDNNGetModelNameList", ret);
    if (model_count != 1 || names == nullptr) {
      return fail("model pack must contain exactly one model");
    }
    if (std::string(names[0]) != "yolo11m_bayese_640x640_nv12") {
      return fail("unexpected model name: " + std::string(names[0]));
    }
    ret = hbDNNGetModelHandle(&model_, packed_, names[0]);
    if (ret != 0) return failCode("hbDNNGetModelHandle", ret);

    int32_t input_count = 0;
    ret = hbDNNGetInputCount(&input_count, model_);
    if (ret != 0) return failCode("hbDNNGetInputCount", ret);
    if (input_count != 1) return fail("model must have exactly one input");
    ret = hbDNNGetInputTensorProperties(&input_properties_, model_, 0);
    if (ret != 0) return failCode("hbDNNGetInputTensorProperties", ret);
    if (input_properties_.tensorType != HB_DNN_IMG_TYPE_NV12 ||
        input_properties_.tensorLayout != HB_DNN_LAYOUT_NCHW ||
        input_properties_.alignedByteSize != 614400 ||
        input_properties_.validShape.numDimensions != 4 ||
        input_properties_.validShape.dimensionSize[0] != 1 ||
        input_properties_.validShape.dimensionSize[1] != 3 ||
        input_properties_.validShape.dimensionSize[2] != 640 ||
        input_properties_.validShape.dimensionSize[3] != 640) {
      return fail("model input is not NV12 NCHW 1x3x640x640/614400");
    }

    int32_t output_count = 0;
    ret = hbDNNGetOutputCount(&output_count, model_);
    if (ret != 0) return failCode("hbDNNGetOutputCount", ret);
    if (output_count != 6) return fail("YOLO11 model must have six outputs");
    outputs_.resize(6);
    const int sizes[3] = {80, 40, 20};
    for (int i = 0; i < 6; ++i) {
      std::memset(&outputs_[i], 0, sizeof(outputs_[i]));
      ret = hbDNNGetOutputTensorProperties(&outputs_[i].properties, model_, i);
      if (ret != 0) return failCode("hbDNNGetOutputTensorProperties", ret);
      const int scale = i / 2;
      const int channels = (i % 2 == 0) ? 80 : 64;
      const auto& properties = outputs_[i].properties;
      if (properties.tensorType != HB_DNN_TENSOR_TYPE_F32 ||
          properties.tensorLayout != HB_DNN_LAYOUT_NHWC ||
          properties.quantiType != NONE ||
          !shapeMatches(properties.validShape, 1, sizes[scale], sizes[scale],
                        channels)) {
        return fail("unexpected YOLO11 output tensor properties");
      }
      ret = hbSysAllocCachedMem(&outputs_[i].sysMem[0],
                                properties.alignedByteSize);
      if (ret != 0) return failCode("hbSysAllocCachedMem(output)", ret);
    }
    initialized_ = true;
    return true;
  }

  bool infer(const ExternalMemory& memory, InferenceResult& result) {
    if (!initialized_ && !initialize()) return false;
    InputBinding binding;
    try {
      binding = makeInputBinding({1, 3, 640, 640, 614400}, memory);
    } catch (const std::exception& error) {
      return fail(error.what());
    }

    hbDNNTensor input;
    std::memset(&input, 0, sizeof(input));
    input.properties = input_properties_;
    input.sysMem[0].phyAddr = binding.phy_addr;
    input.sysMem[0].virAddr = binding.virt_addr;
    input.sysMem[0].memSize = binding.bytes;
    if (input.sysMem[0].phyAddr != memory.phy_addr) {
      return fail("DNN input physical address differs from VSE address");
    }

    hbDNNInferCtrlParam control;
    HB_DNN_INITIALIZE_INFER_CTRL_PARAM(&control);
    control.bpuCoreId = config_.bpu_core_id;
    hbDNNTaskHandle_t task = nullptr;
    hbDNNTensor* output_ptr = outputs_.data();
    const auto bpu_start = Clock::now();
    const auto submit_start = bpu_start;
    int32_t ret = hbDNNInfer(&task, &output_ptr, &input, model_, &control);
    const auto submit_end = Clock::now();
    if (ret != 0) return failCode("hbDNNInfer", ret);
    const auto wait_start = Clock::now();
    ret = hbDNNWaitTaskDone(task, config_.timeout_ms);
    const auto wait_end = Clock::now();
    if (ret != 0) {
      hbDNNReleaseTask(task);
      return failCode("hbDNNWaitTaskDone", ret);
    }
    const auto invalidate_start = Clock::now();
    for (auto& output : outputs_) {
      ret = hbSysFlushMem(&output.sysMem[0], HB_SYS_MEM_CACHE_INVALIDATE);
      if (ret != 0) {
        hbDNNReleaseTask(task);
        return failCode("hbSysFlushMem(output invalidate)", ret);
      }
    }
    const auto invalidate_end = Clock::now();
    const auto release_start = Clock::now();
    ret = hbDNNReleaseTask(task);
    const auto release_end = Clock::now();
    if (ret != 0) return failCode("hbDNNReleaseTask", ret);

    std::array<FloatTensorView, 6> views;
    for (int i = 0; i < 6; ++i) {
      const int size = (i < 2) ? 80 : ((i < 4) ? 40 : 20);
      const int channels = (i % 2 == 0) ? 80 : 64;
      views[i] = {static_cast<const float*>(outputs_[i].sysMem[0].virAddr),
                  size, size, channels};
    }
    try {
      result.detections = postprocessor_.run(views, &result.postprocess);
    } catch (const std::exception& error) {
      return fail(error.what());
    }
    result.dnn_input_phy = input.sysMem[0].phyAddr;
    result.bpu.submit_ms = elapsedMs(submit_start, submit_end);
    result.bpu.wait_ms = elapsedMs(wait_start, wait_end);
    result.bpu.invalidate_ms = elapsedMs(invalidate_start, invalidate_end);
    result.bpu.task_release_ms = elapsedMs(release_start, release_end);
    result.bpu.total_ms = elapsedMs(bpu_start, release_end);
    result.bpu.core_id = config_.bpu_core_id;
    result.bpu_ms = result.bpu.total_ms;
    result.postprocess_ms = result.postprocess.total_ms;
    last_error_.clear();
    return true;
  }

  const std::string& lastError() const { return last_error_; }

 private:
  bool fail(const std::string& message) {
    last_error_ = message;
    if (!initialized_) release();
    return false;
  }

  bool failCode(const char* operation, int32_t code) {
    std::ostringstream message;
    message << operation << " failed with code " << code;
    return fail(message.str());
  }

  void release() {
    for (auto& output : outputs_) {
      if (output.sysMem[0].virAddr != nullptr ||
          output.sysMem[0].phyAddr != 0) {
        hbSysFreeMem(&output.sysMem[0]);
      }
    }
    outputs_.clear();
    if (packed_ != nullptr) hbDNNRelease(packed_);
    packed_ = nullptr;
    model_ = nullptr;
    initialized_ = false;
  }

  Config config_;
  Yolo11Postprocessor postprocessor_;
  hbPackedDNNHandle_t packed_ = nullptr;
  hbDNNHandle_t model_ = nullptr;
  hbDNNTensorProperties input_properties_{};
  std::vector<hbDNNTensor> outputs_;
  bool initialized_ = false;
  std::string last_error_;
};

ZeroCopyYoloRunner::ZeroCopyYoloRunner(Config config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

ZeroCopyYoloRunner::~ZeroCopyYoloRunner() = default;

bool ZeroCopyYoloRunner::initialize() { return impl_->initialize(); }

bool ZeroCopyYoloRunner::infer(const ExternalMemory& memory,
                               InferenceResult& result) {
  return impl_->infer(memory, result);
}

const std::string& ZeroCopyYoloRunner::lastError() const {
  return impl_->lastError();
}

}  // namespace zerocopy
