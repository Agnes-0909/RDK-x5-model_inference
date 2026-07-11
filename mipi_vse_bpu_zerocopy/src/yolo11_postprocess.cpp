#include "yolo11_postprocess.hpp"

#include "detection_mapper.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace zerocopy {
namespace {

constexpr int kClasses = 80;
constexpr int kDflBins = 16;
constexpr int kRegressionChannels = 4 * kDflBins;

float sigmoid(float value) { return 1.0F / (1.0F + std::exp(-value)); }

float intersectionOverUnion(const Box& a, const Box& b) {
  const float left = std::max(a.x1, b.x1);
  const float top = std::max(a.y1, b.y1);
  const float right = std::min(a.x2, b.x2);
  const float bottom = std::min(a.y2, b.y2);
  const float intersection =
      std::max(0.0F, right - left) * std::max(0.0F, bottom - top);
  const float area_a = std::max(0.0F, a.x2 - a.x1) *
                       std::max(0.0F, a.y2 - a.y1);
  const float area_b = std::max(0.0F, b.x2 - b.x1) *
                       std::max(0.0F, b.y2 - b.y1);
  const float combined = area_a + area_b - intersection;
  return combined > 0.0F ? intersection / combined : 0.0F;
}

void validateView(const FloatTensorView& view, int expected_size,
                  int expected_channels, const char* name) {
  if (view.data == nullptr || view.height != expected_size ||
      view.width != expected_size || view.channels != expected_channels) {
    throw std::invalid_argument(std::string("invalid YOLO11 ") + name +
                                " tensor shape");
  }
}

}  // namespace

Yolo11Postprocessor::Yolo11Postprocessor(float score_threshold,
                                         float nms_threshold)
    : score_threshold_(score_threshold), nms_threshold_(nms_threshold) {
  if (!(score_threshold > 0.0F && score_threshold < 1.0F) ||
      !(nms_threshold > 0.0F && nms_threshold < 1.0F)) {
    throw std::invalid_argument("YOLO thresholds must be between zero and one");
  }
}

std::vector<Detection> Yolo11Postprocessor::run(
    const std::array<FloatTensorView, 6>& outputs) const {
  const int sizes[3] = {80, 40, 20};
  const int strides[3] = {8, 16, 32};
  std::vector<Detection> proposals;
  for (int scale = 0; scale < 3; ++scale) {
    const auto& classes = outputs[scale * 2];
    const auto& regression = outputs[scale * 2 + 1];
    validateView(classes, sizes[scale], kClasses, "classification");
    validateView(regression, sizes[scale], kRegressionChannels, "regression");
    decodeScale(classes, regression, strides[scale], proposals);
  }
  return nms(std::move(proposals));
}

void Yolo11Postprocessor::decodeScale(
    const FloatTensorView& classes, const FloatTensorView& regression,
    int stride, std::vector<Detection>& proposals) const {
  const float raw_threshold =
      std::log(score_threshold_ / (1.0F - score_threshold_));
  const std::size_t cells =
      static_cast<std::size_t>(classes.height) * classes.width;
  for (std::size_t cell = 0; cell < cells; ++cell) {
    const float* class_values = classes.data + cell * kClasses;
    int best_class = 0;
    float best_logit = class_values[0];
    for (int class_id = 1; class_id < kClasses; ++class_id) {
      if (class_values[class_id] > best_logit) {
        best_logit = class_values[class_id];
        best_class = class_id;
      }
    }
    if (best_logit < raw_threshold) continue;

    const float* regression_values =
        regression.data + cell * kRegressionChannels;
    float sides[4] = {};
    for (int side = 0; side < 4; ++side) {
      const float* values = regression_values + side * kDflBins;
      float maximum = -std::numeric_limits<float>::infinity();
      for (int bin = 0; bin < kDflBins; ++bin) {
        maximum = std::max(maximum, values[bin]);
      }
      float weighted = 0.0F;
      float sum = 0.0F;
      for (int bin = 0; bin < kDflBins; ++bin) {
        const float probability = std::exp(values[bin] - maximum);
        weighted += probability * static_cast<float>(bin);
        sum += probability;
      }
      sides[side] = sum > 0.0F ? weighted / sum : 0.0F;
    }

    const int grid_y = static_cast<int>(cell / classes.width);
    const int grid_x = static_cast<int>(cell % classes.width);
    const float center_x = (static_cast<float>(grid_x) + 0.5F) * stride;
    const float center_y = (static_cast<float>(grid_y) + 0.5F) * stride;
    Box model_box{center_x - sides[0] * stride,
                  center_y - sides[1] * stride,
                  center_x + sides[2] * stride,
                  center_y + sides[3] * stride};
    proposals.push_back(
        Detection{best_class, sigmoid(best_logit),
                  mapModelBoxToSensor(model_box)});
  }
}

std::vector<Detection> Yolo11Postprocessor::nms(
    std::vector<Detection> proposals) const {
  std::sort(proposals.begin(), proposals.end(),
            [](const Detection& a, const Detection& b) {
              return a.score > b.score;
            });
  std::vector<Detection> kept;
  std::vector<bool> suppressed(proposals.size(), false);
  for (std::size_t i = 0; i < proposals.size(); ++i) {
    if (suppressed[i]) continue;
    kept.push_back(proposals[i]);
    for (std::size_t j = i + 1; j < proposals.size(); ++j) {
      if (!suppressed[j] && proposals[i].class_id == proposals[j].class_id &&
          intersectionOverUnion(proposals[i].box, proposals[j].box) >
              nms_threshold_) {
        suppressed[j] = true;
      }
    }
  }
  return kept;
}

}  // namespace zerocopy
