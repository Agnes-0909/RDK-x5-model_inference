#ifndef MIPI_VSE_BPU_ZEROCOPY_VIO_PIPELINE_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_VIO_PIPELINE_HPP_

#include "vse_frame.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zerocopy {

class AsyncLogger;

struct HardwareSyncTiming {
  int fps = 0;
  std::uint32_t period_us = 0;
  std::uint32_t offset_us = 0;
  std::uint32_t duty_us = 0;
  std::uint32_t trigger_source = 0;
  std::uint32_t trigger_mode = 0;
};

HardwareSyncTiming sc132gsHardwareSyncTiming() noexcept;

bool shouldSetMclkAttribute(bool device_tree_has_pinctrl_names) noexcept;

struct SensorRouting {
  int camera_phy = 0;
  int vin_rx = 0;
  int timestamp_source = 0;
};

SensorRouting makeSensorRouting(int camera_phy, int vcon_rx_phy,
                                int lpwm_channel) noexcept;
std::array<int, 3> sensorPowerSequence(bool active_high) noexcept;
std::optional<int> detectSensorAddress(
    const std::vector<int>& candidates,
    const std::function<bool(int)>& probe);

class CleanupStack {
 public:
  using Cleanup = std::function<void()>;
  ~CleanupStack();
  void push(Cleanup cleanup);
  void run() noexcept;

 private:
  std::vector<Cleanup> cleanups_;
};

class DualVioPipeline {
 public:
  DualVioPipeline(int left_host, int right_host, AsyncLogger* logger = nullptr);
  ~DualVioPipeline();

  DualVioPipeline(const DualVioPipeline&) = delete;
  DualVioPipeline& operator=(const DualVioPipeline&) = delete;

  bool initialize();
  bool acquireLeft(int timeout_ms, VseFrame& frame);
  bool acquireRight(int timeout_ms, VseFrame& frame);
  void stop() noexcept;
  const std::string& lastError() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_VIO_PIPELINE_HPP_
