#include "RenderTest.h"

#include <fmt/core.h>

#include <algorithm>
#include <cstdlib>

namespace rentest {


  TestResult::Verdict TestResult::automatic() const {
    if (skipped()) {
      return Verdict::Skip;
    }
    return failures.empty() ? Verdict::Pass : Verdict::Fail;
  }


  TestResult::Verdict TestResult::verdict() const { return manual != Verdict::None ? manual : automatic(); }


  const char *verdictName(TestResult::Verdict verdict) {
    switch (verdict) {
      case TestResult::Verdict::None:
        return "NONE";
      case TestResult::Verdict::Pass:
        return "PASS";
      case TestResult::Verdict::Fail:
        return "FAIL";
      case TestResult::Verdict::Skip:
        return "SKIP";
    }
    return "?";
  }



  // The pixel of an image that contains uv. uv = 1 is the last pixel, not one
  // past it.
  static glm::uvec2 pixelPosition(const PixelImage &image, glm::vec2 uv) {
    const glm::uvec2 size(image.width, image.height);
    return glm::min(glm::uvec2(glm::vec2(size) * uv), size - 1u);
  }


  void TestContext::checkPixel(std::string_view name, glm::vec2 uv, glm::u8vec4 expected, u8 tolerance) {
    if (!finalImage) {
      fail(fmt::format("{}: there is no final image to check", name));
      return;
    }

    const glm::uvec2 position = pixelPosition(*finalImage, uv);
    const glm::u8vec4 actual = pixel(uv);
    for (int channel = 0; channel < 4; channel++) {
      if (std::abs(int(actual[channel]) - int(expected[channel])) > tolerance) {
        fail(fmt::format("{}: pixel ({}, {}) is ({}, {}, {}, {}), but the test expects ({}, {}, {}, {})", name, position.x, position.y, actual.r,
                         actual.g, actual.b, actual.a, expected.r, expected.g, expected.b, expected.a));
        return;
      }
    }
  }


  glm::u8vec4 TestContext::pixel(glm::vec2 uv) const {
    if (!finalImage) {
      throw std::runtime_error("There is no final image");
    }

    const glm::uvec2 position = pixelPosition(*finalImage, uv);
    const size_t offset = (size_t(position.y) * finalImage->width + position.x) * 4;
    const auto *data = &finalImage->pixels[offset];
    return glm::u8vec4(data[0], data[1], data[2], data[3]);
  }



  ////////////////////////////////////////////////////////////////////////////
  // Registration
  ////////////////////////////////////////////////////////////////////////////

  // A function-local static, so that it exists before the first static
  // TestRegistration in any file.
  static std::vector<TestInfo> &registry() {
    static std::vector<TestInfo> tests;
    return tests;
  }


  TestRegistration::TestRegistration(std::string name, std::function<ren::Box<RenderTest>()> make, TestConfig config, const char *file) {
    registry().push_back(TestInfo{std::move(name), std::move(make), config, file});
  }


  std::vector<const TestInfo *> getTests() {
    std::vector<const TestInfo *> tests;
    for (auto &test : registry()) {
      tests.push_back(&test);
    }

    std::sort(tests.begin(), tests.end(), [](const TestInfo *a, const TestInfo *b) { return a->name < b->name; });
    return tests;
  }

}  // namespace rentest
