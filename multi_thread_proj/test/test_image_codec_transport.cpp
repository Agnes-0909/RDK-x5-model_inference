#include <cstdlib>
#include <iostream>

#include "image_codec.hpp"

int main() {
  mt::ModelInputInfo info{};
  info.input_width = mt::kModelInputWidth;
  info.input_height = mt::kModelInputHeight;
  info.source_width = mt::kFixedWidth;
  info.source_height = mt::kFixedHeight;
  info.pad_x = 10;
  info.pad_y = 20;
  info.scale = 0.5F;

  const auto raw = mt::BuildTransportFrameInfo(info, mt::ImageLoadMode::kSourceFrame);
  if (raw.width != mt::kFixedWidth || raw.height != mt::kFixedHeight ||
      raw.source_width != mt::kFixedWidth ||
      raw.source_height != mt::kFixedHeight || raw.pad_x != 0 ||
      raw.pad_y != 0 || raw.scale != 1.0F || raw.preprocessed) {
    std::cerr << "raw transport info mismatch\n";
    return EXIT_FAILURE;
  }

  const auto model =
      mt::BuildTransportFrameInfo(info, mt::ImageLoadMode::kModelInputPreprocessed);
  if (model.width != mt::kModelInputWidth ||
      model.height != mt::kModelInputHeight ||
      model.source_width != mt::kFixedWidth ||
      model.source_height != mt::kFixedHeight || model.pad_x != 10 ||
      model.pad_y != 20 || model.scale != 0.5F || !model.preprocessed) {
    std::cerr << "model transport info mismatch\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
