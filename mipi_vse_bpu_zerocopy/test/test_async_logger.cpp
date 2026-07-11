#include "async_logger.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                          \
      std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: "           \
                << #condition << '\n';                                           \
      ++failures;                                                                \
    }                                                                            \
  } while (false)

std::filesystem::path temporaryDirectory(const std::string& suffix) {
  const auto path = std::filesystem::temp_directory_path() /
                    ("mipi_async_logger_" + std::to_string(getpid()) + "_" +
                     suffix);
  std::filesystem::remove_all(path);
  return path;
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::vector<std::string> lines;
  for (std::string line; std::getline(input, line);) {
    lines.push_back(line);
  }
  return lines;
}

void testConcurrentEventsAndShutdownFlush() {
  const auto directory = temporaryDirectory("events");
  zerocopy::LoggerConfig config;
  config.directory = directory.string();
  config.min_level = zerocopy::LogLevel::kInfo;
  config.max_bytes = 1024 * 1024;
  config.max_files = 2;
  config.queue_capacity = 8;
  config.console = false;

  zerocopy::AsyncLogger logger(config);
  logger.start();
  logger.log({zerocopy::LogLevel::kDebug, "test", "filtered", "main", "",
              -1, "must not appear", {}});

  auto producer = [&logger](const std::string& channel) {
    for (int index = 0; index < 100; ++index) {
      zerocopy::LogEvent event;
      event.level = zerocopy::LogLevel::kInfo;
      event.component = "worker";
      event.event = "frame_done";
      event.thread = channel + "-worker";
      event.channel = channel;
      event.frame_id = index;
      event.message = "quote=\" slash=\\ newline=\n";
      event.fields = {{"index", static_cast<int64_t>(index)},
                      {"zero_copy", true},
                      {"latency_ms", 1.25}};
      logger.log(std::move(event));
    }
  };

  std::thread left(producer, "left");
  std::thread right(producer, "right");
  left.join();
  right.join();
  logger.log({zerocopy::LogLevel::kWarn, "app", "tail", "main", "", -1,
              "tail event", {{"count", uint64_t{200}}}});
  logger.shutdown();

  const auto text_lines = readLines(directory / "run.log");
  const auto json_lines = readLines(directory / "events.jsonl");
  CHECK(text_lines.size() == 201);
  CHECK(json_lines.size() == text_lines.size());

  std::regex sequence_pattern("\\\"sequence\\\":([0-9]+)");
  for (std::size_t index = 0; index < json_lines.size(); ++index) {
    std::smatch match;
    CHECK(std::regex_search(json_lines[index], match, sequence_pattern));
    if (match.size() == 2) {
      CHECK(std::stoull(match[1].str()) == index + 1);
    }
  }
  CHECK(json_lines.front().find("\"message\":\"quote=\\\" slash=\\\\ newline=\\n\"") !=
        std::string::npos);
  CHECK(json_lines.front().find("\"zero_copy\":true") != std::string::npos);
  CHECK(json_lines.front().find("\"latency_ms\":1.25") != std::string::npos);
  CHECK(json_lines.back().find("\"event\":\"tail\"") != std::string::npos);
  for (const auto& line : json_lines) {
    CHECK(line.find("filtered") == std::string::npos);
  }
  std::filesystem::remove_all(directory);
}

void testRotationLimit() {
  const auto directory = temporaryDirectory("rotation");
  zerocopy::LoggerConfig config;
  config.directory = directory.string();
  config.max_bytes = 512;
  config.max_files = 2;
  config.console = false;
  zerocopy::AsyncLogger logger(config);
  logger.start();
  for (int index = 0; index < 80; ++index) {
    logger.log({zerocopy::LogLevel::kInfo, "rotation", "large_event", "main",
                "", index, std::string(120, 'x'), {}});
  }
  logger.shutdown();

  CHECK(std::filesystem::exists(directory / "run.log"));
  CHECK(std::filesystem::exists(directory / "run.log.1"));
  CHECK(std::filesystem::exists(directory / "run.log.2"));
  CHECK(!std::filesystem::exists(directory / "run.log.3"));
  CHECK(std::filesystem::exists(directory / "events.jsonl"));
  CHECK(std::filesystem::exists(directory / "events.jsonl.1"));
  CHECK(std::filesystem::exists(directory / "events.jsonl.2"));
  CHECK(!std::filesystem::exists(directory / "events.jsonl.3"));
  std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
  testConcurrentEventsAndShutdownFlush();
  testRotationLimit();
  if (failures != 0) return 1;
  std::cout << "test_async_logger: PASS\n";
  return 0;
}
