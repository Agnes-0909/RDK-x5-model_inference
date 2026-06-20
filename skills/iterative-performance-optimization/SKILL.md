---
name: iterative-performance-optimization
description: Use when optimizing runtime, reducing latency, adding missing timing logs, or iterating on bottlenecks for local-build plus remote-board native projects
---

# Iterative Performance Optimization

## Overview

This skill drives an evidence-first optimization loop for native projects that build locally and run on a remote board. It treats performance work as an iterative measurement and validation task, not as ad hoc code tuning.

This skill is reusable. It relies on project adapter scripts for build, deploy, remote run, and log retrieval instead of hardcoding project commands.

The default transport model is local build plus remote-board execution over `ssh` and `scp`. The adapters should preserve that model unless the project explicitly documents a different supported transport.

## When to Use

Use this skill when the task is to:

- optimize runtime or end-to-end latency
- reduce board-side execution time
- analyze logs to find bottlenecks
- add missing timing logs before optimization
- improve async overlap, copy paths, hardware-offload usage, or SIMD performance

Do not use this skill when the work is primarily:

- cloud fleet profiling
- kernel-level profiling as the main workflow
- GUI-driven manual benchmarking
- a project that cannot be built locally before remote validation

## Required Inputs

Before changing code, locate the project adapter scripts:

- `scripts/perf/build.sh`
- `scripts/perf/deploy.sh`
- `scripts/perf/run.sh`
- `scripts/perf/pull_logs.sh`

Optional:

- `scripts/perf/analyze.sh`
- `scripts/perf/env.sh`

If the required scripts are missing, stop and report the exact gap. Do not invent project-specific shell flows when the adapter contract is absent.

Assume this execution model unless the project documents an explicit override:

- local build on the development machine
- `scp` deployment to the board
- `ssh` remote execution on the board
- `scp` retrieval of remote logs

Read these references before acting when needed:

- `references/adapter-contract.md`
- `references/logging-guidelines.md`
- `references/optimization-checklist.md`

## Workflow

Follow this loop strictly.

### 1. Establish execution context

- locate the adapter scripts
- inspect the current timing and profiling facilities
- identify build target, board host, remote workspace, and log paths from the project context

### 2. Check the current evidence

Inspect whether the existing logs can identify the slow stages and their cost. Do not optimize from intuition alone.

### 3. Add missing timing logs if needed

If the current logs are too sparse to isolate bottlenecks, first add or improve timing logs. Rebuild and rerun to create a usable baseline before changing performance-sensitive logic.

### 4. Capture a baseline

- build locally with `scripts/perf/build.sh`
- deploy over `scp` with `scripts/perf/deploy.sh`
- run remotely over `ssh` with `scripts/perf/run.sh`
- pull logs locally over `scp` with `scripts/perf/pull_logs.sh`
- summarize the top bottlenecks with evidence

### 5. Choose one focused optimization

Pick the highest-leverage justified change. Do not mix unrelated optimizations into one pass.

### 6. Verify the effect

Repeat the same build, deploy, run, and pull flow. Compare updated logs against the baseline. Do not claim improvement without fresh measured evidence.

### 7. Iterate or stop

Continue with the next bottleneck when the previous change is verified and the remaining work still has worthwhile expected return. Stop when the goal is met, the remaining gains are low-value, or further work crosses a risk boundary.

## Logging Gate

Treat logging as insufficient when any critical stage lacks at least one of:

- scoped elapsed time
- invocation count
- relevant input-size or batch context
- wait or synchronization time when blocking can occur

For image or inference pipelines, the logs should usually distinguish:

- input acquisition or decode
- preprocess stages such as resize, color conversion, normalize, pack, or crop
- inference submission and completion
- post-process
- memory copy or data movement
- output packaging

For asynchronous pipelines, prefer logs that expose:

- enqueue time
- completion time
- queue depth if available
- producer wait and consumer wait where relevant

If these signals are missing, add logs first. Do not jump straight to NEON or hardware tuning without a usable stage breakdown.

## Optimization Order

Prefer this order unless the evidence clearly points elsewhere:

1. remove unnecessary computation
2. remove avoidable serialization and add safe async or parallel overlap
3. reduce copies and redundant format transforms
4. use platform-specific hardware paths where justified
5. replace slow implementations with more efficient ones
6. apply SIMD or NEON micro-optimization

Use platform-specific paths as candidates, not assumptions. Examples include RGA on RKNN pipelines, VSE or equivalent on Horizon pipelines, and zero-copy handoff opportunities across decode, preprocess, inference input, and postprocess output.

## Risk Boundaries

This skill may directly:

- modify business code
- add timing logs
- change scheduling, memory movement, and implementation details
- run the full validation loop autonomously

Stop for explicit confirmation before:

- changing public interfaces or wire contracts
- changing algorithm semantics in ways that can affect correctness
- replacing one hardware backend with another
- introducing a new third-party dependency
- making a broad architectural rewrite beyond a focused optimization pass

If a change might affect numerical behavior, synchronization semantics, or output ordering, label it as higher risk and confirm before proceeding.

## Required Output Per Iteration

For each optimization pass, report:

- the current bottleneck
- the log evidence
- the selected optimization and rationale
- changed files or modules
- risk notes
- before and after timing comparison
- whether to continue, stop, or escalate

Keep it concise, but do not skip the measured comparison.

## References

- `references/adapter-contract.md`: required adapter scripts and behavior
- `references/logging-guidelines.md`: when to add logs and what to measure
- `references/optimization-checklist.md`: optimization ordering and platform-specific checks
