#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "model_inference_base.hpp"

namespace rdk {

class YoloInfer : public ModelInferenceBase {
 public:
  struct Options {
    int input_width = 640;
    int input_height = 640;
    int num_classes = 80;
    float score_threshold = 0.25F;
    float nms_threshold = 0.45F;
    bool keep_ratio = true;
    int reg = 16;
  };

  struct Detection {
    int class_id = -1;
    float score = 0.0F;
    cv::Rect2f box;
  };

  struct Nv12Frame {
    const uint8_t* data = nullptr;
    const uint8_t* y_plane = nullptr;
    const uint8_t* uv_plane = nullptr;
    int width = 0;
    int height = 0;
    int stride_y = 0;
    int stride_uv = 0;
    int crop_x = 0;
    int crop_y = 0;
    int crop_width = 0;
    int crop_height = 0;
    int source_width = 0;
    int source_height = 0;
    int letterbox_pad_x = 0;
    int letterbox_pad_y = 0;
    float letterbox_scale = 1.0F;
    bool preprocessed = false;
  };

  explicit YoloInfer(ModelInferenceBase::Config config);
  YoloInfer(ModelInferenceBase::Config config, Options options);

  bool infer(const cv::Mat& image);
  bool infer(const Nv12Frame& frame);

  const Options& options() const;
  const std::vector<Detection>& detections() const;
  std::string dumpModelInfo() const;

 protected:
  bool preprocess(const void* input, TensorList& input_tensors) override;
  bool postprocess(const TensorList& output_tensors) override;

 private:
  struct LetterboxInfo {
    float scale = 1.0F;
    int pad_x = 0;
    int pad_y = 0;
    int source_width = 0;
    int source_height = 0;
  };

  float readOutputValue(const hbDNNTensor& tensor, size_t flat_index,
                        int channel) const;
  bool parseYoloOutput(const hbDNNTensor& tensor,
                       std::vector<Detection>& proposals) const;
  bool parseMultiScaleOutputs(const TensorList& output_tensors,
                              std::vector<Detection>& proposals) const;
  bool parseScalePair(const hbDNNTensor& cls_tensor,
                      const hbDNNTensor& reg_tensor, int stride,
                      std::vector<Detection>& proposals) const;
  bool parseScalePairFloatNhwc(const hbDNNTensor& cls_tensor,
                               const hbDNNTensor& reg_tensor, int stride,
                               std::vector<Detection>& proposals) const;
  bool isMultiScaleHead(const TensorList& output_tensors) const;
  size_t tensorOffset(const hbDNNTensor& tensor, int n, int h, int w,
                      int c) const;
  static cv::Mat bgrToNv12(const cv::Mat& bgr);
  static float sigmoid(float value);
  void applyNms(std::vector<Detection>& proposals);
  cv::Rect2f restoreBox(float cx, float cy, float width, float height) const;
  cv::Rect2f restoreBoxFromCorners(float x1, float y1, float x2,
                                   float y2) const;
  static float intersectionOverUnion(const cv::Rect2f& lhs,
                                     const cv::Rect2f& rhs);
  static int elementSize(int32_t tensor_type);
  static size_t shapeElementCount(const hbDNNTensorShape& shape);
  static int dimension(const hbDNNTensorShape& shape, int index);

  Options options_;
  LetterboxInfo letterbox_;
  std::vector<Detection> detections_;
  cv::Mat nv12_letterbox_buffer_;
};

}  // namespace rdk
