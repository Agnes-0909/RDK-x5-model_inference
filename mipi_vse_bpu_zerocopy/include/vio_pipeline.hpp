#ifndef MIPI_VSE_BPU_ZEROCOPY_VIO_PIPELINE_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_VIO_PIPELINE_HPP_

#include "vse_frame.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace zerocopy {

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
  DualVioPipeline(int left_host, int right_host);
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
