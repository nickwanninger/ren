#pragma once

#include <ren/types.h>

#include <vulkan/vulkan.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rentest {

  // An RGBA8 image on the CPU, with tightly packed rows.
  struct PixelImage {
    u32 width = 0, height = 0;
    std::vector<u8> pixels;
  };


  // How readback maps the values of a float image to 8-bit channels. Alpha
  // always uses Linear.
  enum class FloatEncoding {
    Srgb,    // Linear color: clamp to [0, 1], then encode as sRGB. E.g. albedo.
    Linear,  // Data in [0, 1]: clamp. E.g. SSAO.
    Signed,  // Data in [-1, 1]: x * 0.5 + 0.5, then clamp. E.g. normals.
  };


  // Convert the bytes of Image::readPixels() to an RGBA8 image. RGBA8 is
  // copied, BGRA8 is reordered, float formats use encoding, and depth becomes
  // gray (Linear). Throws for other formats.
  PixelImage toPixelImage(VkFormat format, u32 width, u32 height, const std::vector<u8> &bytes, FloatEncoding encoding);


  // The difference between a result and its golden image. Errors are the
  // absolute difference of one channel, in the range 0 to 1.
  struct GoldenComparison {
    float meanError = 0.0f;  // The average over all channels of all pixels.
    float maxError = 0.0f;   // The largest error of one channel.
    size_t differentPixels = 0;
    size_t pixelCount = 0;
  };


  // The golden image of a test: apps/rentest/golden/<name>.png.
  std::filesystem::path goldenPath(const std::string &testName);

  // Where a failed comparison writes its files: <build>/rentest/<name>.<suffix>.png.
  std::filesystem::path outputPath(const std::string &testName, const char *suffix);

  // Return an empty optional if the file does not exist. Throws if the file
  // exists but cannot be read.
  std::optional<PixelImage> loadPng(const std::filesystem::path &path);

  // Make the directory if necessary. Throws if the write fails.
  void writePng(const std::filesystem::path &path, const PixelImage &image);

  // Compare two images of the same size. Throws if the sizes differ.
  GoldenComparison compareImages(const PixelImage &actual, const PixelImage &expected);

  // An image that shows where two images of the same size differ: the error
  // of each color channel, made 8 times brighter, and opaque. An error in
  // alpha shows as white.
  PixelImage makeDiffImage(const PixelImage &actual, const PixelImage &expected);

}  // namespace rentest
