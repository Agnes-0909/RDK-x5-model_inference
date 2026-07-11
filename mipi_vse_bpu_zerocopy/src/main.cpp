#include "app_config.hpp"
#include "async_logger.hpp"
#include "dual_channel_coordinator.hpp"
#include "vio_pipeline.hpp"
#include "zero_copy_channel_processor.hpp"

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "hb_mem_mgr.h"

namespace {

std::atomic_bool signal_stop_requested{false};

void handleSignal(int) { signal_stop_requested.store(true); }

double average(double total, uint64_t count) {
  return count == 0 ? 0.0 : total / static_cast<double>(count);
}

std::string jsonEscape(const std::string& value) {
  std::string result;
  for (const char character : value) {
    if (character == '"' || character == '\\') result.push_back('\\');
    if (character == '\n') {
      result += "\\n";
    } else {
      result.push_back(character);
    }
  }
  return result;
}

void writeChannel(std::ostream& output, const zerocopy::ChannelStats& stats) {
  output << "{\"successes\":" << stats.successes
         << ",\"failures\":" << stats.failures
         << ",\"detections\":" << stats.detections
         << ",\"cache_hits\":" << stats.cache_hits
         << ",\"cache_misses\":" << stats.cache_misses
         << ",\"address_mismatches\":" << stats.address_mismatches
         << ",\"release_errors\":" << stats.release_errors
         << ",\"avg_bpu_ms\":" << average(stats.bpu_ms, stats.successes)
         << ",\"avg_postprocess_ms\":"
         << average(stats.postprocess_ms, stats.successes)
         << ",\"avg_end_to_end_ms\":"
         << average(stats.end_to_end_ms, stats.successes)
         << ",\"queue_push_wait_ms\":" << stats.queue_push_wait_ms
         << ",\"queue_pop_wait_ms\":" << stats.queue_pop_wait_ms << '}';
}

void writeSummary(const std::string& directory,
                  const zerocopy::DualChannelSummary& summary,
                  uint64_t left_timeouts, uint64_t right_timeouts,
                  int exit_code, const std::string& error) {
  std::ofstream output(directory + "/summary.json", std::ios::trunc);
  if (!output) throw std::runtime_error("cannot write summary.json");
  output << "{\n  \"exit_code\": " << exit_code
         << ",\n  \"error\": \"" << jsonEscape(error)
         << "\",\n  \"left_timeouts\": " << left_timeouts
         << ",\n  \"right_timeouts\": " << right_timeouts
         << ",\n  \"left\": ";
  writeChannel(output, summary.left);
  output << ",\n  \"right\": ";
  writeChannel(output, summary.right);
  output << "\n}\n";
}

zerocopy::ZeroCopyYoloRunner::Config runnerConfig(
    const zerocopy::AppConfig& config) {
  zerocopy::ZeroCopyYoloRunner::Config result;
  result.model_path = config.model_path;
  result.timeout_ms = config.dnn_timeout_ms;
  result.score_threshold = config.score_threshold;
  result.nms_threshold = config.nms_threshold;
  result.bpu_core_id = zerocopy::kX5BpuCore0;
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  zerocopy::AppConfig config;
  try {
    config = zerocopy::parseArgs(argc, argv);
  } catch (const std::invalid_argument& error) {
    if (std::string(error.what()) == "help requested") {
      std::cout << zerocopy::usage(argv[0]) << '\n';
      return 0;
    }
    std::cerr << error.what() << '\n' << zerocopy::usage(argv[0]) << '\n';
    return 2;
  }

  zerocopy::LoggerConfig logger_config;
  logger_config.directory = config.log_dir;
  logger_config.min_level = config.log_level;
  logger_config.max_bytes =
      static_cast<uint64_t>(config.log_max_mb) * 1024ULL * 1024ULL;
  logger_config.max_files = config.log_files;
  zerocopy::AsyncLogger logger(logger_config);
  try {
    logger.start();
  } catch (const std::exception& error) {
    std::cerr << "logger initialization failed: " << error.what() << '\n';
    return 1;
  }
  logger.log({zerocopy::LogLevel::kInfo, "app", "startup", "main", "", -1,
              "MIPI-VSE dual zero-copy inference starting",
              {{"bpu_core_index", int64_t{0}},
               {"bpu_core_mask", int64_t{zerocopy::kX5BpuCore0}},
               {"frames_per_channel", int64_t{config.frames}},
               {"left_host", int64_t{config.left_host}},
               {"model", config.model_path},
               {"right_host", int64_t{config.right_host}}}});

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);
  int exit_code = 0;
  std::string fatal_error;
  uint64_t left_timeouts = 0;
  uint64_t right_timeouts = 0;
  zerocopy::DualChannelSummary summary;

  const int memory_result = hb_mem_module_open();
  if (memory_result != 0) {
    fatal_error = "hb_mem_module_open failed with code " +
                  std::to_string(memory_result);
    exit_code = 1;
  } else {
    {
      auto left_processor = zerocopy::makeZeroCopyChannelProcessor(
          "left", runnerConfig(config), &logger);
      auto right_processor = zerocopy::makeZeroCopyChannelProcessor(
          "right", runnerConfig(config), &logger);
      zerocopy::DualChannelCoordinator coordinator(
          std::move(left_processor), std::move(right_processor), &logger);
      zerocopy::DualVioPipeline pipelines(config.left_host, config.right_host,
                                           &logger);
      bool vio_initialized = false;
      try {
        if (!coordinator.initialize()) {
          throw std::runtime_error("model initialization failed: " +
                                   coordinator.lastError());
        }
        logger.log({zerocopy::LogLevel::kInfo, "app", "models_ready", "main",
                    "", -1, "left and right model instances initialized", {}});
        if (!pipelines.initialize()) {
          throw std::runtime_error("VIO initialization failed: " +
                                   pipelines.lastError());
        }
        vio_initialized = true;
        logger.log({zerocopy::LogLevel::kInfo, "vio", "pipelines_started",
                    "main", "", -1,
                    "MIPI -> ISP -> VSE hardware flows started", {}});
        coordinator.start();

        uint64_t dispatched = 0;
        int consecutive_timeouts = 0;
        while (!signal_stop_requested.load() && !coordinator.stopRequested() &&
               (config.frames == 0 || dispatched <
                  static_cast<uint64_t>(config.frames))) {
          zerocopy::VseFrame left_frame;
          zerocopy::VseFrame right_frame;
          const bool left_ok =
              pipelines.acquireLeft(config.frame_timeout_ms, left_frame);
          if (!left_ok) ++left_timeouts;
          const bool right_ok =
              pipelines.acquireRight(config.frame_timeout_ms, right_frame);
          if (!right_ok) ++right_timeouts;
          if (!left_ok || !right_ok) {
            ++consecutive_timeouts;
            logger.log({zerocopy::LogLevel::kWarn, "vio", "acquire_timeout",
                        "main", "", -1, pipelines.lastError(),
                        {{"consecutive", int64_t{consecutive_timeouts}},
                         {"left_ok", left_ok}, {"right_ok", right_ok}}});
            if (consecutive_timeouts >= config.max_consecutive_timeouts) {
              throw std::runtime_error("too many consecutive frame timeouts");
            }
            continue;
          }
          consecutive_timeouts = 0;
          if (!coordinator.enqueuePair(std::move(left_frame),
                                       std::move(right_frame))) {
            break;
          }
          ++dispatched;
        }
        coordinator.close();
        coordinator.join();
        if (coordinator.stopRequested()) {
          throw std::runtime_error(coordinator.lastError());
        }
      } catch (const std::exception& error) {
        fatal_error = error.what();
        exit_code = 1;
        coordinator.close();
        coordinator.join();
        logger.log({zerocopy::LogLevel::kError, "app", "fatal_unwind", "main",
                    "", -1, fatal_error, {}});
      }

      summary = coordinator.summary();
      const int unregister_result = coordinator.shutdownProcessors();
      if (unregister_result != 0 && exit_code == 0) {
        fatal_error = "buffer unregistration failed with code " +
                      std::to_string(unregister_result);
        exit_code = 1;
      }
      if (vio_initialized) pipelines.stop();
      logger.log({zerocopy::LogLevel::kInfo, "vio", "pipelines_stopped",
                  "main", "", -1, "VIO hardware resources stopped", {}});
    }
    hb_mem_module_close();
  }

  try {
    writeSummary(config.log_dir, summary, left_timeouts, right_timeouts,
                 exit_code, fatal_error);
  } catch (const std::exception& error) {
    logger.log({zerocopy::LogLevel::kError, "summary", "write_failed", "main",
                "", -1, error.what(), {}});
    exit_code = 1;
  }
  logger.log({zerocopy::LogLevel::kInfo, "summary", "run_complete", "main",
              "", -1, exit_code == 0 ? "run completed" : "run failed",
              {{"address_mismatches",
                summary.left.address_mismatches +
                    summary.right.address_mismatches},
               {"errors", summary.left.failures + summary.right.failures},
               {"exit_code", int64_t{exit_code}},
               {"left_success", summary.left.successes},
               {"right_success", summary.right.successes}}});
  logger.shutdown();
  return exit_code;
}
