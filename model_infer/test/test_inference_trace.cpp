#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "inference_trace.hpp"

namespace {

bool Expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << "\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  using rdk::ClearInferenceTraceContext;
  using rdk::FormatInferenceTraceContext;
  using rdk::GetInferenceTraceContext;
  using rdk::InferenceTraceContext;
  using rdk::SetInferenceTraceContext;

  bool ok = true;

  ClearInferenceTraceContext();
  ok &= Expect(FormatInferenceTraceContext() == "trace=unset",
               "default context should be unset");

  SetInferenceTraceContext(InferenceTraceContext{
      .batch_id = 11, .channel_id = 2, .frame_id = 33, .valid = true});
  ok &= Expect(FormatInferenceTraceContext() == "batch=11 channel=2 frame_id=33",
               "formatted trace context mismatch");

  const auto current = GetInferenceTraceContext();
  ok &= Expect(current.valid && current.batch_id == 11 &&
                   current.channel_id == 2 && current.frame_id == 33,
               "stored context mismatch");

  std::string child_value = "not-run";
  std::thread child([&child_value] {
    child_value = FormatInferenceTraceContext();
    SetInferenceTraceContext(rdk::InferenceTraceContext{
        .batch_id = 7, .channel_id = 1, .frame_id = 9, .valid = true});
    child_value += "|" + FormatInferenceTraceContext();
  });
  child.join();

  ok &= Expect(child_value == "trace=unset|batch=7 channel=1 frame_id=9",
               "thread-local trace context should not leak across threads");
  ok &= Expect(FormatInferenceTraceContext() == "batch=11 channel=2 frame_id=33",
               "child thread should not overwrite parent trace context");

  ClearInferenceTraceContext();
  ok &= Expect(FormatInferenceTraceContext() == "trace=unset",
               "clear should reset trace context");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
