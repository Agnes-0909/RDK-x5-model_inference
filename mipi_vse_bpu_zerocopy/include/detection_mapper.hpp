#ifndef MIPI_VSE_BPU_ZEROCOPY_DETECTION_MAPPER_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_DETECTION_MAPPER_HPP_

#include "detection.hpp"

namespace zerocopy {

constexpr int kSensorWidth = 1088;
constexpr int kSensorHeight = 1280;
constexpr int kCropX = 0;
constexpr int kCropY = 96;
constexpr int kCropSize = 1088;
constexpr int kModelSize = 640;

Box mapModelBoxToSensor(const Box& model_box);

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_DETECTION_MAPPER_HPP_
