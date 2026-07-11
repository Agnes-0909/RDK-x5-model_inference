#ifndef MIPI_VSE_BPU_ZEROCOPY_ZERO_COPY_YOLO_RUNNER_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_ZERO_COPY_YOLO_RUNNER_HPP_

#include "buffer_layout.hpp"
#include "detection.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zerocopy {

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
};

class ZeroCopyYoloRunner {
 public:
  struct Config {
    std::string model_path;
    int timeout_ms = 5000;
    float score_threshold = 0.25F;
    float nms_threshold = 0.45F;
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
