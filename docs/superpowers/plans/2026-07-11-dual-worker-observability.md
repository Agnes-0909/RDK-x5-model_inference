# Dual Worker Observability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add complete asynchronous text/JSON logging and move left/right zero-copy inference into independent worker threads with separate model instances on the single X5 BPU core.

**Architecture:** A producer acquires paired VSE frames and moves them into two capacity-one blocking queues. Each `ChannelWorker` owns its runner, registration cache, postprocessor and statistics, while one asynchronous writer serializes typed events to console, rotating text/JSONL files and final summary.

**Tech Stack:** C++17, pthread/std::thread, CMake/CTest, Horizon DNN/VIO SDK, Docker aarch64 cross-build, X5 hardware validation.

---

## File map

- Create `include/log_event.hpp`, `include/async_logger.hpp`, `src/async_logger.cpp`: event types, async writer, JSON/text formatting and rotation.
- Create `include/bounded_frame_queue.hpp`, `src/bounded_frame_queue.cpp`: capacity-one move-only frame queue.
- Create `include/channel_worker.hpp`, `src/channel_worker.cpp`: per-channel runner/cache/thread/stats ownership.
- Modify `include/app_config.hpp`, `src/app_config.cpp`: log directory, level and rotation CLI.
- Modify `include/zero_copy_yolo_runner.hpp`, `src/zero_copy_yolo_runner.cpp`: explicit core 0 and stage timings/events.
- Modify `include/yolo11_postprocess.hpp`, `src/yolo11_postprocess.cpp`: postprocess stage timing/candidate counts.
- Modify `include/vio_pipeline.hpp`, `src/vio_pipeline.cpp`: sensor/VIO/acquire/release events.
- Modify `src/main.cpp`: producer/coordinator lifecycle and summary JSON.
- Modify `scripts/run_on_board.sh`: preserve current user arguments; add timestamped directory and `console.log` tee.
- Add `test/test_async_logger.cpp`, `test/test_bounded_frame_queue.cpp`, `test/test_channel_worker.cpp`; extend existing tests.

### Task 1: Logging configuration

**Files:** Modify `include/app_config.hpp`, `src/app_config.cpp`, `test/test_app_config.cpp`.

- [ ] Add failing assertions for defaults `log_dir="logs"`, level `info`, max MB `100`, files `5`, plus parsing `--log-dir /tmp/run --log-level debug --log-max-mb 2 --log-files 3` and rejection of unknown level/zero sizes.
- [ ] Run RED with host clang command; expect missing `AppConfig` fields or parser options.
- [ ] Add `LogLevel` parsing and validated fields. Extend usage with the four exact options.
- [ ] Run GREEN and existing config tests; expect `test_app_config: PASS`.
- [ ] Commit only config/test files with `git commit -m "feat: configure runtime logging"`.

### Task 2: Asynchronous dual-format logger

**Files:** Create `include/log_event.hpp`, `include/async_logger.hpp`, `src/async_logger.cpp`, `test/test_async_logger.cpp`; modify `CMakeLists.txt`.

- [ ] Write a failing test that starts a logger in a temporary directory, submits events from two threads, shuts down, then asserts unique contiguous sequence values, equal text/JSONL event counts, escaped JSON strings, level filtering, and tail-event persistence.
- [ ] Add a rotation test using `max_bytes=512` and `max_files=2`; assert base plus `.1`/`.2` exist and `.3` does not.
- [ ] Run RED; expect missing logger types.
- [ ] Implement `LogValue` as `variant<string,int64_t,uint64_t,double,bool>`, `LogEvent`, `LoggerConfig`, and `AsyncLogger::start/log/shutdown`. The writer owns files and rotation; business threads only enqueue.
- [ ] Serialize one human-readable line and one valid JSON object per accepted event. Assign sequence under the queue mutex; block instead of dropping when the internal event queue is full.
- [ ] Run GREEN plus all existing host tests.
- [ ] Commit with `git commit -m "feat: add asynchronous text and json logger"`.

### Task 3: Capacity-one frame queue

**Files:** Create `include/bounded_frame_queue.hpp`, `src/bounded_frame_queue.cpp`, `test/test_bounded_frame_queue.cpp`; modify `CMakeLists.txt`.

- [ ] Write failing tests using move-only `VseFrame`: FIFO move ownership, producer blocks while full, pop wakes producer, close wakes blocked push/pop, queued/destructed frames release exactly once.
- [ ] Run RED; expect missing queue.
- [ ] Implement queue with mutex, `not_empty`, `not_full`, capacity constructor fixed to one in production, `push(VseFrame&&)`, `pop(VseFrame&)`, `close()`, `size()` and blocking duration return values.
- [ ] Run GREEN and lifecycle regression tests.
- [ ] Commit with `git commit -m "feat: add blocking VSE frame queue"`.

### Task 4: Explicit BPU core and postprocess breakdown

**Files:** Modify `include/zero_copy_yolo_runner.hpp`, `src/zero_copy_yolo_runner.cpp`, `include/yolo11_postprocess.hpp`, `src/yolo11_postprocess.cpp`, `test/test_tensor_binding.cpp`, `test/test_yolo11_postprocess.cpp`.

- [ ] Extend failing tests for default `bpu_core_id=HB_BPU_CORE_0` through an SDK-independent integer constant and for `PostprocessMetrics` containing `class_scan_ms`, `dfl_decode_ms`, `map_ms`, `nms_ms`, `proposals`, `detections`.
- [ ] Run RED; expect missing config/metrics.
- [ ] Set `control.bpuCoreId=config_.bpu_core_id`; reject values other than core 0 in this X5 application. Split postprocessing timers without changing decoded detections.
- [ ] Add runner metrics for submit, wait, invalidate, task release and total BPU time; exclude logger time.
- [ ] Run GREEN and Docker-build `mipi_zerocopy_dnn`.
- [ ] Commit with `git commit -m "feat: expose BPU and postprocess stage timings"`.

### Task 5: Independent channel workers

**Files:** Create `include/channel_worker.hpp`, `src/channel_worker.cpp`, `test/test_channel_worker.cpp`; modify `CMakeLists.txt`.

- [ ] Write failing tests with injected processor callbacks: left/right use different processor identities, process concurrently, update separate stats, release after callback completion, propagate first error, close queue and join cleanly.
- [ ] Run RED; expect missing worker.
- [ ] Implement `ChannelWorker` with channel name, capacity-one queue, thread, one `ZeroCopyYoloRunner`, one `RegisteredBufferCache`, stats and logger reference. Initialize two model instances sequentially before `start()`.
- [ ] Worker loop: pop frame, validate/register, infer core 0, postprocess, emit events, update stats, release frame. On failure publish shared error and close both queues through coordinator callback.
- [ ] Run GREEN plus queue/resource tests.
- [ ] Commit with `git commit -m "feat: add independent left and right inference workers"`.

### Task 6: Main lifecycle and component events

**Files:** Modify `src/main.cpp`, `include/vio_pipeline.hpp`, `src/vio_pipeline.cpp`, `include/registered_buffer_cache.hpp`, `src/registered_buffer_cache.cpp`.

- [ ] Add failing coordinator test that enqueues equal left/right counts, blocks rather than drops, closes queues, joins, and verifies summary equals processed frames.
- [ ] Instrument stable events for app config, sensor probe, VIO node creation/binding/start/stop, frame acquire/release, memory register/hit/unregister, DNN stages, postprocess stages, queue wait and fatal unwind.
- [ ] Replace sequential `inferFrame` with producer dispatch. For `--frames N`, enqueue exactly N frames per channel, close both queues, join, unregister caches, then stop VIO.
- [ ] Write `summary.json` after join with per-channel counts/timings, queue waits, cache counts and address mismatches.
- [ ] Run all host tests and `check_no_image_copy.cmake`; expect zero failures and no input-copy APIs.
- [ ] Commit with `git commit -m "feat: run dual zero-copy inference workers"`.

### Task 7: Board runner and documentation

**Files:** Modify `scripts/run_on_board.sh`, `README.md`; preserve the pre-existing user worktree version of the script.

- [ ] Save the current script diff before editing and assert its model/frames/score/NMS behavior remains present.
- [ ] Add timestamped `LOG_ROOT/run_YYYYMMDD_HHMMSS`, pass logging options, and pipe combined stdout/stderr through `tee console.log` under `set -o pipefail`.
- [ ] Document environment overrides `MODEL`, `FRAMES`, `SCORE`, `NMS`, `LOG_ROOT`, `LOG_LEVEL`, plus four output files and `jq` examples.
- [ ] Docker full-build from `/project/mipi_vse_bpu_zerocopy`; verify aarch64 ELF and policy gate.
- [ ] Commit project changes while ensuring no unrelated user file changes are staged.

### Task 8: X5 hardware acceptance

**Files:** Modify project files only after reproducing any failure with a regression test.

- [ ] Deploy binary and script to `/root/mipi_vse_bpu_zerocopy`.
- [ ] Run 2 frames per side; assert two model-load events, left/right worker events, core 0, frame address equality and clean shutdown.
- [ ] Run 100 frames per side. Parse `events.jsonl` with Python/json, verify 200 infer events, zero address mismatches, no acquired-frame drops, three registrations per side and successful exit.
- [ ] Verify `run.log`, `events.jsonl`, `console.log`, `summary.json`; compare summary counts to JSONL.
- [ ] Run fresh host tests, Docker full build, static copy scan and `git diff --check`.
- [ ] Commit board-proven fixes with `git commit -m "fix: validate dual workers and logging on X5"`.
