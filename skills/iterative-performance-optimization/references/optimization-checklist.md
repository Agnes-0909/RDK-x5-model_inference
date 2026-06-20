# Optimization Checklist

Use this checklist after a measured baseline exists. Do not treat it as a menu to apply blindly. Pick the highest-leverage category supported by the logs.

## Priority Order

1. remove unnecessary computation
2. add async or parallel overlap
3. reduce copies and format churn
4. use hardware-specific paths
5. replace slow implementations
6. apply SIMD or NEON tuning

This order is deliberate. Most systems gain more from removing wasted work, overlap, and copies than from starting with micro-optimization.

## 1. Remove Unnecessary Computation

Check for:

- repeated preprocessing of unchanged inputs
- duplicated resize, crop, normalize, or conversion steps
- post-process work executed for discarded results
- expensive debug paths left enabled
- format transforms that are later undone

Use this category when the logs show a stage runs more often than necessary or does work whose result is not needed.

## 2. Add Async or Parallel Overlap

Check for:

- decode, preprocess, inference, and post-process happening strictly in serial
- idle CPU or accelerator windows between dependent stages
- queues that are mostly empty because producers and consumers are synchronized too tightly
- board-side I/O or transport blocking useful compute

Use this category when the logs show wait time, idle gaps, or obvious serialization that can be overlapped safely.

## 3. Reduce Copies and Format Churn

Check for:

- repeated host-to-host copies
- extra copy-in or copy-out around inference
- redundant buffer materialization between stages
- multiple color or layout conversions on the same frame

Prefer zero-copy or fewer-copy handoff when the logs show copy-heavy cost. Do not assume zero-copy is present unless the boundaries are measured.

## 4. Use Hardware-Specific Paths

Check for existing but unused acceleration opportunities.

Examples:

- RKNN pipelines: use RGA where resize, crop, color conversion, or data movement is currently CPU-heavy
- Horizon pipelines: inspect VSE or equivalent preprocess acceleration where CPU preprocess dominates
- any accelerator pipeline: move work closer to the hardware path if the current bottleneck is staging or transfer

Use this category when the logs show CPU preprocessing or transfer overhead that a known hardware path can plausibly reduce.

## 5. Replace Slow Implementations

Check for:

- generic library code in a proven hotspot
- scalar helper paths in critical loops
- repeated allocation or poor data structure choices
- unnecessary virtual dispatch or abstraction cost in a hot section

Use this category when the logs have already isolated a hot implementation and higher-order improvements are exhausted or not applicable.

## 6. Apply SIMD or NEON Tuning

Check for:

- tight arithmetic loops that dominate runtime after earlier fixes
- per-pixel or per-element kernels with stable memory access patterns
- scalar fallback paths on ARM64 where NEON is appropriate

Use this category late, unless the logs clearly show a compute kernel dominates and the surrounding pipeline is already clean.

## Evidence Rules

Before taking a category, state:

- which log lines identify the bottleneck
- why this category fits better than the earlier categories
- what result should improve if the change works

If the change does not materially improve the measured bottleneck, mark the attempt ineffective and move on instead of defending it by intuition.

## Platform-Specific Reminders

- RKNN: check RGA before hand-writing CPU preprocess optimizations when image transform cost dominates
- Horizon: check VSE or equivalent before deep CPU tuning when preprocess dominates
- ARM64: treat NEON as a later-stage tool, not the default first move
- all platforms: inspect zero-copy opportunities before expensive micro-tuning if copies are visible in the logs

## Stop Conditions

Stop iterating when:

- the target is met
- the remaining bottlenecks are low-value
- the next change would cross a risk boundary
- the evidence is too weak to choose the next move confidently

Escalate for confirmation when the next improvement requires interface changes, algorithm semantic changes, backend swaps, or broad architectural rewrites.
