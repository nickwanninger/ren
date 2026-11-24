#include "Golden.h"

#include <fmt/core.h>
#include <glm/gtc/packing.hpp>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace rentest {


  // Map one float channel to a byte.
  static u8 encodeChannel(float value, FloatEncoding encoding) {
    switch (encoding) {
      case FloatEncoding::Srgb: {
        const float linear = glm::clamp(value, 0.0f, 1.0f);
        value = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        break;
      }
      case FloatEncoding::Linear:
        break;
      case FloatEncoding::Signed:
        value = value * 0.5f + 0.5f;
        break;
    }
    return u8(std::round(glm::clamp(value, 0.0f, 1.0f) * 255.0f));
  }


  PixelImage toPixelImage(VkFormat format, u32 width, u32 height, const std::vector<u8> &bytes, FloatEncoding encoding) {
    PixelImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(size_t(width) * height * 4);
    const size_t pixelCount = size_t(width) * height;

    // Write one pixel from four float channels.
    auto store = [&](size_t pixel, glm::vec4 value) {
      u8 *out = &image.pixels[pixel * 4];
      out[0] = encodeChannel(value.r, encoding);
      out[1] = encodeChannel(value.g, encoding);
      out[2] = encodeChannel(value.b, encoding);
      out[3] = encodeChannel(value.a, FloatEncoding::Linear);
    };

    switch (format) {
      case VK_FORMAT_R8G8B8A8_UNORM:
      case VK_FORMAT_R8G8B8A8_SRGB:
        image.pixels = bytes;
        break;

      case VK_FORMAT_B8G8R8A8_UNORM:
      case VK_FORMAT_B8G8R8A8_SRGB:
        for (size_t i = 0; i < pixelCount; i++) {
          const u8 *in = &bytes[i * 4];
          u8 *out = &image.pixels[i * 4];
          out[0] = in[2];
          out[1] = in[1];
          out[2] = in[0];
          out[3] = in[3];
        }
        break;

      case VK_FORMAT_R16G16B16A16_SFLOAT: {
        const auto *in = reinterpret_cast<const u16 *>(bytes.data());
        for (size_t i = 0; i < pixelCount; i++) {
          const u16 *h = &in[i * 4];
          store(i, glm::vec4(glm::unpackHalf1x16(h[0]), glm::unpackHalf1x16(h[1]), glm::unpackHalf1x16(h[2]), glm::unpackHalf1x16(h[3])));
        }
        break;
      }

      case VK_FORMAT_R32G32B32A32_SFLOAT: {
        const auto *in = reinterpret_cast<const glm::vec4 *>(bytes.data());
        for (size_t i = 0; i < pixelCount; i++) {
          store(i, in[i]);
        }
        break;
      }

      case VK_FORMAT_D32_SFLOAT:
      case VK_FORMAT_D32_SFLOAT_S8_UINT: {
        const auto *in = reinterpret_cast<const float *>(bytes.data());
        for (size_t i = 0; i < pixelCount; i++) {
          const u8 gray = encodeChannel(in[i], FloatEncoding::Linear);
          u8 *out = &image.pixels[i * 4];
          out[0] = out[1] = out[2] = gray;
          out[3] = 255;
        }
        break;
      }

      default:
        throw std::runtime_error(fmt::format("The harness cannot convert format {} to RGBA8", (int)format));
    }
    return image;
  }


  std::filesystem::path goldenPath(const std::string &testName) { return std::filesystem::path(RENTEST_GOLDEN_DIR) / (testName + ".png"); }


  std::filesystem::path outputPath(const std::string &testName, const char *suffix) {
    return std::filesystem::path(RENTEST_OUTPUT_DIR) / fmt::format("{}.{}.png", testName, suffix);
  }


  std::optional<PixelImage> loadPng(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) {
      return std::nullopt;
    }

    // Always ask for four channels, so that the layout matches readPixels().
    int width = 0, height = 0, channels = 0;
    u8 *data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!data) {
      throw std::runtime_error(fmt::format("Cannot read {}: {}", path.string(), stbi_failure_reason()));
    }

    PixelImage image;
    image.width = u32(width);
    image.height = u32(height);
    image.pixels.assign(data, data + size_t(width) * height * 4);
    stbi_image_free(data);
    return image;
  }


  void writePng(const std::filesystem::path &path, const PixelImage &image) {
    std::filesystem::create_directories(path.parent_path());
    if (!stbi_write_png(path.string().c_str(), int(image.width), int(image.height), 4, image.pixels.data(), int(image.width * 4))) {
      throw std::runtime_error("Cannot write " + path.string());
    }
  }


  GoldenComparison compareImages(const PixelImage &actual, const PixelImage &expected) {
    if (actual.width != expected.width || actual.height != expected.height) {
      throw std::runtime_error(
          fmt::format("The result is {}x{}, but the golden image is {}x{}", actual.width, actual.height, expected.width, expected.height));
    }

    GoldenComparison result;
    result.pixelCount = size_t(actual.width) * actual.height;

    u64 errorSum = 0;
    int maxError = 0;
    for (size_t pixel = 0; pixel < result.pixelCount; pixel++) {
      bool differs = false;
      for (size_t channel = 0; channel < 4; channel++) {
        const size_t i = pixel * 4 + channel;
        const int error = std::abs(int(actual.pixels[i]) - int(expected.pixels[i]));
        errorSum += error;
        maxError = std::max(maxError, error);
        differs |= error != 0;
      }
      if (differs) {
        result.differentPixels++;
      }
    }

    result.meanError = float(double(errorSum) / (double(result.pixelCount) * 4.0 * 255.0));
    result.maxError = float(maxError) / 255.0f;
    return result;
  }


  PixelImage makeDiffImage(const PixelImage &actual, const PixelImage &expected) {
    PixelImage diff;
    diff.width = actual.width;
    diff.height = actual.height;
    diff.pixels.resize(actual.pixels.size());

    for (size_t i = 0; i < diff.pixels.size(); i += 4) {
      // An error in alpha shows as white, because alpha has no color.
      const int alpha = std::abs(int(actual.pixels[i + 3]) - int(expected.pixels[i + 3]));
      for (size_t channel = 0; channel < 3; channel++) {
        const int error = std::abs(int(actual.pixels[i + channel]) - int(expected.pixels[i + channel]));
        diff.pixels[i + channel] = u8(std::min(255, std::max(error, alpha) * 8));
      }
      diff.pixels[i + 3] = 255;
    }
    return diff;
  }

}  // namespace rentest
