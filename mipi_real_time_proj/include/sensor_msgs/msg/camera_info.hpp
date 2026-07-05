#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sensor_msgs {
namespace msg {

struct CameraInfo {
  uint32_t width = 0;
  uint32_t height = 0;
  std::string distortion_model;
  std::vector<double> d;
  std::array<double, 9> k{};
  std::array<double, 9> r{};
  std::array<double, 12> p{};
};

}  // namespace msg
}  // namespace sensor_msgs

namespace builtin_interfaces {
namespace msg {

struct Time {
  int32_t sec = 0;
  uint32_t nanosec = 0;
};

}  // namespace msg
}  // namespace builtin_interfaces
