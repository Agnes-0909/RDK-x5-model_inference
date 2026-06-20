# Logging Guidelines

This skill treats missing timing visibility as a blocker. Before optimizing, confirm that the logs are dense enough to isolate the real bottleneck.

## When Logging Is Insufficient

Logging is insufficient when a critical stage lacks any of the following:

- elapsed time for the stage
- invocation count
- input-size, frame-size, or batch-size context where it affects cost
- wait or synchronization time when the stage can block

If multiple stages collapse into one coarse timing number, the logs are usually still insufficient for targeted optimization.

## Minimum Useful Coverage

Every critical stage should ideally report:

- stage name
- elapsed time
- invocation count or frame index
- relevant input characteristics
- error or fallback status if a fast path is skipped

For repeated workloads, prefer logs that make it possible to estimate:

- average time
- worst-case time
- frequency of slow outliers

## Recommended Stage Taxonomy

For image and inference pipelines, separate logs for:

- input acquisition
- decode
- crop or ROI extraction
- resize
- color conversion
- normalize or pack
- inference submission
- inference completion
- post-process
- result packaging
- memory copy or format conversion

Do not hide expensive pre-process work behind one generic "process" label if the internal stages can be split cheaply.

## Asynchronous Systems

For asynchronous or pipelined code, measure more than just compute time.

Useful signals include:

- enqueue time
- dequeue or completion time
- queue depth if available
- producer wait time
- consumer wait time
- synchronization wait time

If async code only logs callback completion without queue or wait information, the bottleneck analysis may still be incomplete.

## When To Add Logs First

Add or improve logs before optimization when:

- the slowest stage cannot be identified confidently
- total latency is known but stage breakdown is missing
- a queueing or synchronization bottleneck is suspected but not measured
- copy cost is suspected but not separated from compute cost
- hardware-offload paths exist but their entry, exit, or fallback timing is not visible

Rebuild and rerun after adding logs so the baseline reflects the improved visibility.

## Keep Logs Useful

The goal is useful evidence, not noise.

Prefer:

- stable stage names
- one scoped timing record per meaningful stage
- enough context to compare runs
- logs that can be grepped or aggregated mechanically

Avoid:

- flooding every inner loop with verbose logs unless the hot spot is already isolated there
- changing log labels across runs without reason
- mixing unrelated counters into timing lines without structure

## Hardware and Data-Movement Visibility

If the project uses hardware acceleration, the logs should help distinguish:

- CPU-side preparation
- hardware submission
- hardware completion
- fallback-to-CPU cases
- copy-in and copy-out overhead

If the project is expected to be zero-copy or near zero-copy, add logs around copy boundaries so the assumption can be verified instead of guessed.
