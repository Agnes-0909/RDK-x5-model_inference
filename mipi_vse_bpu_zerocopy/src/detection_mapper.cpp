#include "detection_mapper.hpp"

#include <algorithm>

namespace zerocopy {
namespace {

float clamp(float value, float low, float high) {
  return std::max(low, std::min(value, high));
}

}  // namespace

Box mapModelBoxToSensor(const Box& model_box) {
  constexpr float scale = static_cast<float>(kCropSize) / kModelSize;
  Box mapped;
  mapped.x1 = clamp(model_box.x1 * scale + kCropX, 0.0F,
                    static_cast<float>(kSensorWidth));
  mapped.y1 = clamp(model_box.y1 * scale + kCropY, 0.0F,
                    static_cast<float>(kSensorHeight));
  mapped.x2 = clamp(model_box.x2 * scale + kCropX, 0.0F,
                    static_cast<float>(kSensorWidth));
  mapped.y2 = clamp(model_box.y2 * scale + kCropY, 0.0F,
                    static_cast<float>(kSensorHeight));
  if (mapped.x2 < mapped.x1) mapped.x2 = mapped.x1;
  if (mapped.y2 < mapped.y1) mapped.y2 = mapped.y1;
  return mapped;
}

}  // namespace zerocopy
