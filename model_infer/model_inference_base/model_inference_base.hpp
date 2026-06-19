#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "dnn/hb_dnn.h"
#include "dnn/hb_dnn_status.h"

namespace rdk {

class ModelInferenceBase {
 public:
  struct Config {
    std::string model_file;
    std::string model_name;
    int32_t timeout_ms = 5000;
    int32_t bpu_core_id = HB_BPU_CORE_ANY;
    int32_t dsp_core_id = HB_DSP_CORE_ANY;
    int32_t priority = HB_DNN_PRIORITY_LOWEST;
  };

  explicit ModelInferenceBase(Config config);
  virtual ~ModelInferenceBase();

  ModelInferenceBase(const ModelInferenceBase&) = delete;
  ModelInferenceBase& operator=(const ModelInferenceBase&) = delete;

  bool initialize();
  void release();
  bool isInitialized() const;

  bool infer(const void* input);

  const Config& config() const;
  const std::string& modelName() const;
  const std::string& lastError() const;
  const std::vector<hbDNNTensor>& inputTensors() const;
  const std::vector<hbDNNTensor>& outputTensors() const;
  const std::vector<hbDNNTensorProperties>& inputProperties() const;
  const std::vector<hbDNNTensorProperties>& outputProperties() const;

 protected:
  using TensorList = std::vector<hbDNNTensor>;

  virtual bool preprocess(const void* input, TensorList& input_tensors) = 0;
  virtual bool postprocess(const TensorList& output_tensors) = 0;

  virtual bool prepareInputTensors();
  virtual bool prepareOutputTensors();

  bool execute();
  bool allocateTensorBuffer(hbDNNTensor& tensor) const;
  void releaseTensorBuffer(hbDNNTensor& tensor) const;
  bool flushTensor(hbDNNTensor& tensor, int32_t flag) const;
  bool flushInputTensor(size_t index) const;
  bool invalidateOutputTensor(size_t index) const;
  bool copyToInputTensor(size_t index, const void* data, size_t bytes);

  template <typename T>
  T* tensorData(hbDNNTensor& tensor) const {
    return static_cast<T*>(tensor.sysMem[0].virAddr);
  }

  template <typename T>
  const T* tensorData(const hbDNNTensor& tensor) const {
    return static_cast<const T*>(tensor.sysMem[0].virAddr);
  }

  static size_t alignedByteSize(const hbDNNTensorProperties& properties);
  static std::string shapeToString(const hbDNNTensorShape& shape);

  void setLastError(std::string message) const;
  void setDnnError(const std::string& api, int32_t code) const;

 private:
  bool loadModel();
  bool queryModelInfo();
  void releaseTask(hbDNNTaskHandle_t task_handle) const;

  Config config_;
  std::string resolved_model_name_;
  hbPackedDNNHandle_t packed_handle_ = nullptr;
  hbDNNHandle_t dnn_handle_ = nullptr;
  bool initialized_ = false;

  TensorList input_tensors_;
  TensorList output_tensors_;
  std::vector<hbDNNTensorProperties> input_properties_;
  std::vector<hbDNNTensorProperties> output_properties_;
  mutable std::string last_error_;
};

}  // namespace rdk
