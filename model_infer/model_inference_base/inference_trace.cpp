#include "inference_trace.hpp"

#include <sstream>

namespace rdk {
namespace {

thread_local InferenceTraceContext g_inference_trace_context;

}  // namespace

void SetInferenceTraceContext(const InferenceTraceContext& context) {
  g_inference_trace_context = context;
}

void ClearInferenceTraceContext() { g_inference_trace_context = {}; }

InferenceTraceContext GetInferenceTraceContext() {
  return g_inference_trace_context;
}

std::string FormatInferenceTraceContext() {
  const auto context = GetInferenceTraceContext();
  if (!context.valid) {
    return "trace=unset";
  }

  std::ostringstream oss;
  oss << "batch=" << context.batch_id << " channel=" << context.channel_id
      << " frame_id=" << context.frame_id;
  return oss.str();
}

}  // namespace rdk
