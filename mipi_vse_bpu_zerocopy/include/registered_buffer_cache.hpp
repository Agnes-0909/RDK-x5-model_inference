#ifndef MIPI_VSE_BPU_ZEROCOPY_REGISTERED_BUFFER_CACHE_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_REGISTERED_BUFFER_CACHE_HPP_

#include "buffer_layout.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace zerocopy {

struct RegisteredBuffer {
  ExternalMemory memory;
  bool cache_hit = false;
};

class RegisteredBufferCache {
 public:
  using MemoryOperation = std::function<int(ExternalMemory&)>;

  RegisteredBufferCache(MemoryOperation register_memory,
                        MemoryOperation unregister_memory);
  ~RegisteredBufferCache();

  RegisteredBufferCache(const RegisteredBufferCache&) = delete;
  RegisteredBufferCache& operator=(const RegisteredBufferCache&) = delete;

  RegisteredBuffer getOrRegister(const BufferLayout& layout);
  int clear() noexcept;
  std::size_t size() const noexcept;

 private:
  MemoryOperation register_memory_;
  MemoryOperation unregister_memory_;
  std::unordered_map<std::uint64_t, ExternalMemory> buffers_;
};

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_REGISTERED_BUFFER_CACHE_HPP_
