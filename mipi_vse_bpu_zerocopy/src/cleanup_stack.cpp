#include "vio_pipeline.hpp"

#include <utility>

namespace zerocopy {

CleanupStack::~CleanupStack() { run(); }

void CleanupStack::push(Cleanup cleanup) {
  if (cleanup) cleanups_.push_back(std::move(cleanup));
}

void CleanupStack::run() noexcept {
  while (!cleanups_.empty()) {
    Cleanup cleanup = std::move(cleanups_.back());
    cleanups_.pop_back();
    try {
      cleanup();
    } catch (...) {
    }
  }
}

}  // namespace zerocopy
