#ifndef MIPI_VSE_BPU_ZEROCOPY_VSE_FRAME_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_VSE_FRAME_HPP_

#include "buffer_layout.hpp"

#include <cstdint>
#include <functional>

namespace zerocopy {

class VseFrame {
 public:
  using ReleaseFunction = std::function<int()>;

  VseFrame() = default;
  VseFrame(std::uint64_t frame_id, BufferLayout layout,
           ReleaseFunction release);
  ~VseFrame();

  VseFrame(const VseFrame&) = delete;
  VseFrame& operator=(const VseFrame&) = delete;
  VseFrame(VseFrame&& other) noexcept;
  VseFrame& operator=(VseFrame&& other) noexcept;

  bool valid() const noexcept;
  std::uint64_t frameId() const noexcept;
  const BufferLayout& layout() const noexcept;
  int release() noexcept;

 private:
  std::uint64_t frame_id_ = 0;
  BufferLayout layout_;
  ReleaseFunction release_;
  bool valid_ = false;
};

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_VSE_FRAME_HPP_
