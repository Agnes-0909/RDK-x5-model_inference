#include "async_logger.hpp"

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace zerocopy {
namespace {

bool accepts(LogLevel level, LogLevel minimum) {
  return static_cast<int>(level) >= static_cast<int>(minimum);
}

std::string jsonEscape(const std::string& value) {
  std::ostringstream output;
  for (const unsigned char character : value) {
    switch (character) {
      case '"': output << "\\\""; break;
      case '\\': output << "\\\\"; break;
      case '\b': output << "\\b"; break;
      case '\f': output << "\\f"; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default:
        if (character < 0x20U) {
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(character) << std::dec;
        } else {
          output << static_cast<char>(character);
        }
    }
  }
  return output.str();
}

std::string textEscape(const std::string& value) {
  std::ostringstream output;
  for (const char character : value) {
    switch (character) {
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default: output << character; break;
    }
  }
  return output.str();
}

std::string valueAsJson(const LogValue& value) {
  return std::visit(
      [](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return std::string("\"") + jsonEscape(item) + "\"";
        } else if constexpr (std::is_same_v<T, bool>) {
          return std::string(item ? "true" : "false");
        } else {
          std::ostringstream output;
          output << item;
          return output.str();
        }
      },
      value);
}

std::string eventAsJson(const LogEvent& event) {
  std::ostringstream output;
  output << "{\"wall_time\":\"" << jsonEscape(event.wall_time)
         << "\",\"monotonic_us\":" << event.monotonic_us
         << ",\"sequence\":" << event.sequence << ",\"level\":\""
         << logLevelName(event.level) << "\",\"thread\":\""
         << jsonEscape(event.thread) << "\",\"component\":\""
         << jsonEscape(event.component) << "\",\"event\":\""
         << jsonEscape(event.event) << "\",\"channel\":\""
         << jsonEscape(event.channel) << "\",\"frame_id\":";
  if (event.frame_id < 0) {
    output << "null";
  } else {
    output << event.frame_id;
  }
  output << ",\"message\":\"" << jsonEscape(event.message)
         << "\",\"fields\":{";
  bool first = true;
  for (const auto& [name, value] : event.fields) {
    if (!first) output << ',';
    first = false;
    output << '"' << jsonEscape(name) << "\":" << valueAsJson(value);
  }
  output << "}}";
  return output.str();
}

std::string eventAsText(const LogEvent& event) {
  std::ostringstream output;
  output << event.wall_time << " seq=" << event.sequence << ' '
         << logLevelName(event.level) << " [" << event.thread << "] "
         << event.component << '.' << event.event;
  if (!event.channel.empty()) output << " channel=" << event.channel;
  if (event.frame_id >= 0) output << " frame=" << event.frame_id;
  if (!event.message.empty()) output << " - " << textEscape(event.message);
  for (const auto& [name, value] : event.fields) {
    output << ' ' << name << '=';
    std::visit([&output](const auto& item) { output << item; }, value);
  }
  return output.str();
}

std::string currentWallTime() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now.time_since_epoch()) %
                            1000;
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  std::ostringstream output;
  output << std::put_time(&local, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
         << std::setfill('0') << milliseconds.count()
         << std::put_time(&local, "%z");
  return output.str();
}

class RotatingFile {
 public:
  RotatingFile(std::filesystem::path path, uint64_t max_bytes, int max_files)
      : path_(std::move(path)), max_bytes_(max_bytes), max_files_(max_files) {}

  void open() {
    stream_.open(path_, std::ios::out | std::ios::app | std::ios::binary);
    if (!stream_) throw std::runtime_error("cannot open log file: " + path_.string());
    std::error_code error;
    size_ = std::filesystem::file_size(path_, error);
    if (error) size_ = 0;
  }

  void writeLine(const std::string& line) {
    const uint64_t bytes = line.size() + 1;
    if (size_ > 0 && size_ + bytes > max_bytes_) rotate();
    stream_ << line << '\n';
    if (!stream_) throw std::runtime_error("cannot write log file: " + path_.string());
    size_ += bytes;
  }

  void flush() { stream_.flush(); }

 private:
  void rotate() {
    stream_.close();
    std::error_code error;
    std::filesystem::remove(path_.string() + "." + std::to_string(max_files_), error);
    for (int index = max_files_; index >= 2; --index) {
      const auto source = path_.string() + "." + std::to_string(index - 1);
      const auto destination = path_.string() + "." + std::to_string(index);
      std::filesystem::rename(source, destination, error);
      error.clear();
    }
    std::filesystem::rename(path_, path_.string() + ".1", error);
    error.clear();
    size_ = 0;
    stream_.open(path_, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!stream_) throw std::runtime_error("cannot rotate log file: " + path_.string());
  }

  std::filesystem::path path_;
  uint64_t max_bytes_;
  int max_files_;
  uint64_t size_ = 0;
  std::ofstream stream_;
};

}  // namespace

const char* logLevelName(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::kDebug: return "debug";
    case LogLevel::kInfo: return "info";
    case LogLevel::kWarn: return "warn";
    case LogLevel::kError: return "error";
  }
  return "unknown";
}

class AsyncLogger::Impl {
 public:
  explicit Impl(LoggerConfig config)
      : config_(std::move(config)), started_at_(std::chrono::steady_clock::now()) {
    if (config_.directory.empty() || config_.max_bytes == 0 ||
        config_.max_files <= 0 || config_.queue_capacity == 0) {
      throw std::invalid_argument("invalid logger configuration");
    }
  }

  void start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) throw std::logic_error("logger already started");
    std::filesystem::create_directories(config_.directory);
    text_file_ = std::make_unique<RotatingFile>(
        std::filesystem::path(config_.directory) / "run.log",
        config_.max_bytes, config_.max_files);
    json_file_ = std::make_unique<RotatingFile>(
        std::filesystem::path(config_.directory) / "events.jsonl",
        config_.max_bytes, config_.max_files);
    text_file_->open();
    json_file_->open();
    running_ = true;
    accepting_ = true;
    writer_ = std::thread(&Impl::writerLoop, this);
  }

  void log(LogEvent event) {
    if (!accepts(event.level, config_.min_level)) return;
    std::unique_lock<std::mutex> lock(mutex_);
    if (!accepting_) throw std::logic_error("logger is not accepting events");
    not_full_.wait(lock, [this] {
      return queue_.size() < config_.queue_capacity || !accepting_;
    });
    if (!accepting_) throw std::logic_error("logger stopped while logging");
    event.sequence = ++next_sequence_;
    event.monotonic_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started_at_)
            .count());
    event.wall_time = currentWallTime();
    queue_.push_back(std::move(event));
    not_empty_.notify_one();
  }

  void shutdown() noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_) return;
      accepting_ = false;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
    if (writer_.joinable()) writer_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
  }

 private:
  void writerLoop() noexcept {
    try {
      while (true) {
        LogEvent event;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          not_empty_.wait(lock,
                          [this] { return !queue_.empty() || !accepting_; });
          if (queue_.empty() && !accepting_) break;
          event = std::move(queue_.front());
          queue_.pop_front();
          not_full_.notify_one();
        }
        const std::string text = eventAsText(event);
        text_file_->writeLine(text);
        json_file_->writeLine(eventAsJson(event));
        if (config_.console) std::cout << text << '\n';
      }
      text_file_->flush();
      json_file_->flush();
      if (config_.console) std::cout.flush();
    } catch (const std::exception& error) {
      std::cerr << "async logger writer failed: " << error.what() << '\n';
    }
  }

  LoggerConfig config_;
  std::chrono::steady_clock::time_point started_at_;
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::deque<LogEvent> queue_;
  uint64_t next_sequence_ = 0;
  bool running_ = false;
  bool accepting_ = false;
  std::thread writer_;
  std::unique_ptr<RotatingFile> text_file_;
  std::unique_ptr<RotatingFile> json_file_;
};

AsyncLogger::AsyncLogger(LoggerConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

AsyncLogger::~AsyncLogger() { impl_->shutdown(); }

void AsyncLogger::start() { impl_->start(); }

void AsyncLogger::log(LogEvent event) { impl_->log(std::move(event)); }

void AsyncLogger::shutdown() noexcept { impl_->shutdown(); }

}  // namespace zerocopy
