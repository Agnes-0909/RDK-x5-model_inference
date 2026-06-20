#pragma once

#include <cstdint>
#include <string>

namespace rdk {

struct InferenceTraceContext {
  uint64_t batch_id = 0;
  uint32_t channel_id = 0;
  uint64_t frame_id = 0;
  bool valid = false;
};

void SetInferenceTraceContext(const InferenceTraceContext& context);
void ClearInferenceTraceContext();
InferenceTraceContext GetInferenceTraceContext();
std::string FormatInferenceTraceContext();

}  // namespace rdk
