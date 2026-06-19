# Model Inference Base

`ModelInferenceBase` 封装了地平线 RDK DNN 模型的加载、tensor 分配、同步推理和资源释放。具体模型只需要继承后实现预处理和后处理。

## 继承方式

```cpp
#include "model_inference_base.hpp"
#include <opencv2/core.hpp>

class MyModel : public rdk::ModelInferenceBase {
 public:
  using ModelInferenceBase::ModelInferenceBase;

  bool infer(const cv::Mat& image) {
    return ModelInferenceBase::infer(&image);
  }

 protected:
  bool preprocess(const void* input, TensorList& input_tensors) override {
    const auto& image = *static_cast<const cv::Mat*>(input);
    // 1. resize / color convert / normalize / quantize
    // 2. 写入 input_tensors[0].sysMem[0].virAddr
    // 3. 调用 copyToInputTensor(0, data, bytes) 或 flushInputTensor(0)
    return true;
  }

  bool postprocess(const TensorList& output_tensors) override {
    // 读取 output_tensors[*].sysMem[0].virAddr 并解析结果。
    return true;
  }
};
```

## 使用方式

```cpp
rdk::ModelInferenceBase::Config config;
config.model_file = "model.bin";
config.timeout_ms = 5000;

MyModel model(config);
if (!model.initialize()) {
  std::cerr << model.lastError() << std::endl;
  return -1;
}

cv::Mat image;
if (!model.infer(image)) {
  std::cerr << model.lastError() << std::endl;
}
```

## 构建

本目录的 `CMakeLists.txt` 默认使用：

- `../../3rdlibrary/DNN/include` 作为 DNN 头文件目录
- 链接 RDK 系统中的 `dnn` 运行库

基类本身不强依赖 OpenCV。具体模型如果在预处理里使用 `cv::Mat`，可以在业务工程中 `find_package(OpenCV REQUIRED ...)`，并将 `OpenCV_DIR` 指到从 `3rdlibrary/opencv_src` 交叉编译得到的 aarch64 安装目录。
