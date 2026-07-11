#ifndef MIPI_VSE_BPU_ZEROCOPY_ZERO_COPY_YOLO_RUNNER_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_ZERO_COPY_YOLO_RUNNER_HPP_

#include "buffer_layout.hpp"
#include "detection.hpp"
#include "yolo11_postprocess.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zerocopy {

constexpr int kX5BpuCore0 = 1;

struct ModelInputSpec {
  int batch = 0;
  int channels = 0;
  int height = 0;
  int width = 0;
  std::uint32_t bytes = 0;
};

struct InputBinding {
  std::uint64_t phy_addr = 0;
  void* virt_addr = nullptr;
  std::uint32_t bytes = 0;
};

InputBinding makeInputBinding(const ModelInputSpec& spec,
                              const ExternalMemory& memory);

struct InferenceResult {
  std::vector<Detection> detections;
  std::uint64_t dnn_input_phy = 0;
  double bpu_ms = 0.0;
  double postprocess_ms = 0.0;
  struct BpuMetrics {
    double submit_ms = 0.0;
    double wait_ms = 0.0;
    double invalidate_ms = 0.0;
    double task_release_ms = 0.0;
    double total_ms = 0.0;
    int core_index = 0;
    int core_mask = kX5BpuCore0;
  } bpu;
  PostprocessMetrics postprocess;
};

class ZeroCopyYoloRunner {
 public:
  struct Config {
    std::string model_path;
    int timeout_ms = 5000;
    float score_threshold = 0.25F;
    float nms_threshold = 0.45F;
    int bpu_core_id = kX5BpuCore0;
  };

  explicit ZeroCopyYoloRunner(Config config);
  ~ZeroCopyYoloRunner();

  ZeroCopyYoloRunner(const ZeroCopyYoloRunner&) = delete;
  ZeroCopyYoloRunner& operator=(const ZeroCopyYoloRunner&) = delete;

  bool initialize();
  bool infer(const ExternalMemory& memory, InferenceResult& result);
  const std::string& lastError() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_ZERO_COPY_YOLO_RUNNER_HPP_
