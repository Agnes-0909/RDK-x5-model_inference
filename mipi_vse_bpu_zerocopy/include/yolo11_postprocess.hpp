#ifndef MIPI_VSE_BPU_ZEROCOPY_YOLO11_POSTPROCESS_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_YOLO11_POSTPROCESS_HPP_

#include "detection.hpp"

#include <array>
#include <vector>

namespace zerocopy {

struct FloatTensorView {
  const float* data = nullptr;
  int height = 0;
  int width = 0;
  int channels = 0;
};

class Yolo11Postprocessor {
 public:
  Yolo11Postprocessor(float score_threshold, float nms_threshold);

  std::vector<Detection> run(
      const std::array<FloatTensorView, 6>& outputs) const;

 private:
  void decodeScale(const FloatTensorView& classes,
                   const FloatTensorView& regression, int stride,
                   std::vector<Detection>& proposals) const;
  std::vector<Detection> nms(std::vector<Detection> proposals) const;

  float score_threshold_;
  float nms_threshold_;
};

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_YOLO11_POSTPROCESS_HPP_
