#include "registered_buffer_cache.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace zerocopy {

RegisteredBufferCache::RegisteredBufferCache(
    MemoryOperation register_memory, MemoryOperation unregister_memory)
    : register_memory_(std::move(register_memory)),
      unregister_memory_(std::move(unregister_memory)) {
  if (!register_memory_ || !unregister_memory_) {
    throw std::invalid_argument("registration callbacks must be provided");
  }
}

RegisteredBufferCache::~RegisteredBufferCache() { clear(); }

RegisteredBuffer RegisteredBufferCache::getOrRegister(
    const BufferLayout& layout) {
  ExternalMemory memory = validateZeroCopyLayout(layout);
  const auto found = buffers_.find(memory.phy_addr);
  if (found != buffers_.end()) {
    if (found->second.virt_addr != memory.virt_addr ||
        found->second.bytes != memory.bytes) {
      throw std::runtime_error(
          "VSE buffer descriptor changed for a registered physical address");
    }
    return RegisteredBuffer{found->second, true};
  }

  const int ret = register_memory_(memory);
  if (ret != 0) {
    std::ostringstream message;
    message << "hbSysRegisterMem failed with code " << ret;
    throw std::runtime_error(message.str());
  }
  buffers_.emplace(memory.phy_addr, memory);
  return RegisteredBuffer{memory, false};
}

int RegisteredBufferCache::clear() noexcept {
  int first_error = 0;
  for (auto& entry : buffers_) {
    const int ret = unregister_memory_(entry.second);
    if (first_error == 0 && ret != 0) first_error = ret;
  }
  buffers_.clear();
  return first_error;
}

std::size_t RegisteredBufferCache::size() const noexcept {
  return buffers_.size();
}

}  // namespace zerocopy
