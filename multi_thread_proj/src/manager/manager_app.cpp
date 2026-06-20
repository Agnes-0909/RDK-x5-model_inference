#include "manager_app.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cstdint>
#include <sstream>

#include "image_codec.hpp"
#include "logger.hpp"
#include "server_types.hpp"

namespace mt {

namespace fs = std::filesystem;

bool ParseManagerArgs(int argc, char** argv, ManagerConfig& config) {
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    auto requireValue = [&](const std::string& name) -> const char* {
      if (i + 1 >= argc) {
        MT_LOG_ERROR(std::string("Missing value for ") + name);
        return nullptr;
      }
      return argv[++i];
    };

    if (key == "--input-dir-0") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.input_dir_0 = value;
    } else if (key == "--input-dir-1") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.input_dir_1 = value;
    } else if (key == "--input-dir-2") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.input_dir_2 = value;
    } else if (key == "--uds-path") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.uds_path = value;
    } else if (key == "--uds-path-secondary") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.uds_path_secondary = value;
    } else if (key == "--shm-path") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.shm_path = value;
    } else {
      MT_LOG_ERROR(std::string("Unknown argument: ") + key);
      return false;
    }
  }

  return !config.input_dir_0.empty() && !config.input_dir_1.empty() &&
         !config.input_dir_2.empty();
}

bool ManagerApp::initialize(const ManagerConfig& config) {
  config_ = config;
  queue_ = SharedMemoryQueue(SharedMemoryConfig{false, config_.shm_path});
  if (!queue_.initialize()) {
    MT_LOG_ERROR(queue_.lastError());
    return false;
  }
  if (!socket_.connectTo(config_.uds_path)) {
    MT_LOG_ERROR(socket_.lastError());
    return false;
  }
  if (!config_.uds_path_secondary.empty() &&
      !secondary_socket_.connectTo(config_.uds_path_secondary)) {
    MT_LOG_ERROR(secondary_socket_.lastError());
    return false;
  }
  return true;
}

int ManagerApp::run() {
  std::array<std::vector<std::string>, kChannelCount> files;
  if (!collectInputs(files)) {
    return 1;
  }
  std::array<std::vector<PreparedInput>, kChannelCount> prepared_inputs;
  if (!prepareInputs(files, prepared_inputs)) {
    return 1;
  }

  uint64_t batch_id = 1;
  std::array<size_t, kChannelCount> file_indices{0, 0, 0};
  std::array<uint64_t, kChannelCount> next_frame_ids{1, 2, 3};

  {
    std::ostringstream oss;
    oss << "initialized uds_path=" << config_.uds_path
        << " uds_path_secondary="
        << (config_.uds_path_secondary.empty() ? "<disabled>"
                                               : config_.uds_path_secondary)
        << " shm_path=" << config_.shm_path;
    MT_LOG_INFO(oss.str());
  }
  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    std::ostringstream oss;
    oss << "channel=" << channel << " file_count=" << files[channel].size();
    MT_LOG_INFO(oss.str());
  }

  while (true) {
    const auto batch_start = std::chrono::steady_clock::now();
    const std::array<ServerRoute, 2> routes = {{
        ServerRoute{config_.uds_path_secondary.empty() ? 0x7U : 0x3U, &socket_},
        ServerRoute{0x4, config_.uds_path_secondary.empty() ? nullptr
                                                            : &secondary_socket_},
    }};
    std::array<uint64_t, kChannelCount> batch_frame_ids{};

    {
      std::ostringstream oss;
      oss << "preparing batch=" << batch_id;
      MT_LOG_INFO(oss.str());
    }

    for (uint32_t channel = 0; channel < kChannelCount; ++channel) {
      const PreparedInput& prepared =
          prepared_inputs[channel][file_indices[channel]];
      const auto channel_start = std::chrono::steady_clock::now();
      {
        std::ostringstream oss;
        oss << "batch=" << batch_id
            << " channel=" << channel
            << " source=" << prepared.source_path;
        MT_LOG_INFO(oss.str());
      }

      FrameMetadata metadata{};
      metadata.batch_id = batch_id;
      metadata.channel_id = channel;
      metadata.frame_id = next_frame_ids[channel];
      const TransportFrameInfo transport =
          BuildTransportFrameInfo(prepared.model_input_info,
                                  ImageLoadMode::kSourceFrame);
      metadata.width = transport.width;
      metadata.height = transport.height;
      metadata.payload_size =
          static_cast<uint32_t>(prepared.nv12_buffer.size());
      metadata.crop_width = metadata.width;
      metadata.crop_height = metadata.height;
      metadata.source_width = transport.source_width;
      metadata.source_height = transport.source_height;
      metadata.letterbox_pad_x = transport.pad_x;
      metadata.letterbox_pad_y = transport.pad_y;
      metadata.letterbox_scale = transport.scale;
      metadata.preprocessed = transport.preprocessed ? 1U : 0U;
      const auto push_start = std::chrono::steady_clock::now();
      if (!queue_.push(channel, metadata, prepared.nv12_buffer.data(),
                       prepared.nv12_buffer.size())) {
        std::ostringstream oss;
        oss << "push failed batch=" << batch_id
            << " channel=" << channel;
        MT_LOG_ERROR(oss.str());
        return 1;
      }
      const auto push_end = std::chrono::steady_clock::now();
      batch_frame_ids[channel] = metadata.frame_id;
      {
        std::ostringstream oss;
        oss << "queued batch=" << batch_id
            << " channel=" << channel
            << " frame_id=" << metadata.frame_id
            << " bytes=" << metadata.payload_size;
        MT_LOG_INFO(oss.str());
      }
      {
        const auto channel_end = std::chrono::steady_clock::now();
        std::ostringstream oss;
        oss << "stage batch=" << batch_id
            << " channel=" << channel
            << " load_ms=0"
            << " imread_ms=0"
            << " resize_ms=0"
            << " bgr_to_i420_ms=0"
            << " pack_nv12_ms=0"
            << " push_ms=" << elapsedMs(push_start, push_end)
            << " channel_total_ms=" << elapsedMs(channel_start, channel_end);
        MT_LOG_INFO(oss.str());
      }

      file_indices[channel] = (file_indices[channel] + 1) % files[channel].size();
      next_frame_ids[channel] += kChannelCount;
    }

    std::vector<BatchResultMessage> partial_results;
    partial_results.reserve(routes.size());
    for (const ServerRoute& route : routes) {
      if (route.socket == nullptr || route.channel_mask == 0) {
        continue;
      }

      NotifyMessage notify{};
      notify.batch_id = batch_id;
      notify.notify_timestamp_ns =
          static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    batch_start.time_since_epoch())
                                    .count());
      notify.channel_mask = route.channel_mask;
      for (uint32_t channel = 0; channel < kChannelCount; ++channel) {
        if ((route.channel_mask & (1U << channel)) != 0) {
          notify.frame_ids[channel] = batch_frame_ids[channel];
        }
      }

      if (!route.socket->sendAll(&notify, sizeof(notify))) {
        MT_LOG_ERROR(route.socket->lastError());
        return 1;
      }
      {
        std::ostringstream oss;
        oss << "notified batch=" << notify.batch_id
            << " channel_mask=0x" << std::hex << notify.channel_mask << std::dec;
        MT_LOG_INFO(oss.str());
      }
    }

    for (const ServerRoute& route : routes) {
      if (route.socket == nullptr || route.channel_mask == 0) {
        continue;
      }
      BatchResultMessage partial{};
      if (!route.socket->recvAll(&partial, sizeof(partial))) {
        MT_LOG_ERROR(route.socket->lastError());
        return 1;
      }
      partial_results.push_back(partial);
    }

    BatchResultMessage result{};
    if (!mergeBatchResult(partial_results, batch_id, result)) {
      MT_LOG_ERROR("failed to merge partial batch results");
      return 1;
    }
    {
      const auto batch_end = std::chrono::steady_clock::now();
      std::ostringstream oss;
      oss << "result batch=" << result.batch_id
          << " overall_status=" << result.overall_status
          << " latency_ms=" << elapsedMs(batch_start, batch_end);
      MT_LOG_INFO(oss.str());
    }
    for (size_t channel = 0; channel < kChannelCount; ++channel) {
      const ChannelResultRecord& record = result.channel_results[channel];
      std::ostringstream oss;
      oss << "result batch=" << result.batch_id
          << " channel=" << record.channel_id
          << " frame_id=" << record.frame_id
          << " status=" << record.status_code
          << " detect_count=" << record.detect_count;
      MT_LOG_INFO(oss.str());
    }

    ++batch_id;
  }
}

bool ManagerApp::mergeBatchResult(const std::vector<BatchResultMessage>& partials,
                                  uint64_t batch_id,
                                  BatchResultMessage& merged_result) {
  if (partials.empty()) {
    return false;
  }

  merged_result = BatchResultMessage{};
  merged_result.batch_id = batch_id;
  merged_result.channel_count = kChannelCount;
  merged_result.overall_status = kStatusOk;

  std::array<bool, kChannelCount> seen{};
  for (const BatchResultMessage& partial : partials) {
    if (partial.batch_id != batch_id) {
      return false;
    }
    for (size_t channel = 0; channel < kChannelCount; ++channel) {
      const ChannelResultRecord& record = partial.channel_results[channel];
      if (record.channel_id >= kChannelCount || record.frame_id == 0) {
        continue;
      }
      merged_result.channel_results[record.channel_id] = record;
      if (record.status_code != kStatusOk) {
        merged_result.overall_status = record.status_code;
      }
      seen[record.channel_id] = true;
    }
  }

  return std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
}

bool ManagerApp::collectInputs(
    std::array<std::vector<std::string>, kChannelCount>& files) {
  const std::array<std::string, kChannelCount> dirs = {
      config_.input_dir_0, config_.input_dir_1, config_.input_dir_2};
  for (size_t channel = 0; channel < dirs.size(); ++channel) {
    for (const auto& entry : fs::directory_iterator(dirs[channel])) {
      if (entry.is_regular_file()) {
        files[channel].push_back(entry.path().string());
      }
    }
    std::sort(files[channel].begin(), files[channel].end());
    if (files[channel].empty()) {
      MT_LOG_ERROR(std::string("no input files in ") + dirs[channel]);
      return false;
    }
  }
  return true;
}

bool ManagerApp::prepareInputs(
    const std::array<std::vector<std::string>, kChannelCount>& files,
    std::array<std::vector<PreparedInput>, kChannelCount>& prepared_inputs) {
  for (size_t channel = 0; channel < files.size(); ++channel) {
    for (const std::string& image_path : files[channel]) {
      std::vector<uint8_t> nv12_buffer;
      std::string error;
      ImageLoadMetrics image_metrics{};
      ModelInputInfo model_input_info{};
      const auto prepare_start = std::chrono::steady_clock::now();
      if (!loadImageAsNv12(image_path, ImageLoadMode::kSourceFrame,
                           nv12_buffer, error, &image_metrics,
                           &model_input_info)) {
        MT_LOG_ERROR(error);
        return false;
      }
      const auto prepare_end = std::chrono::steady_clock::now();
      PreparedInput prepared{};
      prepared.source_path = image_path;
      prepared.nv12_buffer = std::move(nv12_buffer);
      prepared.model_input_info = model_input_info;
      prepared_inputs[channel].push_back(std::move(prepared));

      std::ostringstream oss;
      oss << "prepared channel=" << channel
          << " source=" << image_path
          << " load_ms=" << image_metrics.total_ms
          << " imread_ms=" << image_metrics.imread_ms
          << " resize_ms=" << image_metrics.resize_ms
          << " bgr_to_i420_ms=" << image_metrics.bgr_to_i420_ms
          << " pack_nv12_ms=" << image_metrics.pack_nv12_ms
          << " prepare_total_ms=" << elapsedMs(prepare_start, prepare_end)
          << " bytes=" << prepared_inputs[channel].back().nv12_buffer.size();
      MT_LOG_INFO(oss.str());
    }
  }
  return true;
}

}  // namespace mt
