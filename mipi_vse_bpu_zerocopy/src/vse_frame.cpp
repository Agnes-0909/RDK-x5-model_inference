#include "vse_frame.hpp"

#include <utility>

namespace zerocopy {

VseFrame::VseFrame(std::uint64_t frame_id, BufferLayout layout,
                   ReleaseFunction release, std::uint64_t hardware_timestamp)
    : frame_id_(frame_id),
      hardware_timestamp_(hardware_timestamp),
      layout_(layout),
      release_(std::move(release)),
      valid_(static_cast<bool>(release_)) {}

VseFrame::~VseFrame() { release(); }

VseFrame::VseFrame(VseFrame&& other) noexcept
    : frame_id_(other.frame_id_),
      hardware_timestamp_(other.hardware_timestamp_),
      layout_(other.layout_),
      release_(std::move(other.release_)),
      valid_(other.valid_) {
  other.valid_ = false;
}

VseFrame& VseFrame::operator=(VseFrame&& other) noexcept {
  if (this == &other) return *this;
  release();
  frame_id_ = other.frame_id_;
  hardware_timestamp_ = other.hardware_timestamp_;
  layout_ = other.layout_;
  release_ = std::move(other.release_);
  valid_ = other.valid_;
  other.valid_ = false;
  return *this;
}

bool VseFrame::valid() const noexcept { return valid_; }

std::uint64_t VseFrame::frameId() const noexcept { return frame_id_; }

std::uint64_t VseFrame::hardwareTimestamp() const noexcept {
  return hardware_timestamp_;
}

const BufferLayout& VseFrame::layout() const noexcept { return layout_; }

int VseFrame::release() noexcept {
  if (!valid_) return 0;
  valid_ = false;
  return release_ ? release_() : 0;
}

}  // namespace zerocopy
