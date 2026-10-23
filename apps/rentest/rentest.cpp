#include "RenderTest.h"
#include "TestWindow.h"

#include <ren/core/Application.h>
#include <ren/core/Flag.h>
#include <ren/core/Logging.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/vulkan/Vulkan.h>

#include <fmt/color.h>

#include <set>

static ren::Flag<std::string> testFilter("test", "*", "Run only the tests whose names match this glob (* and ?).");
static ren::Flag<bool> listTests("list", false, "Print the names of the tests, then exit.");
static ren::Flag<bool> interactiveFlag("interactive", false, "Keep each test open until a person gives a verdict.");
static ren::Flag<bool> saveGoldenFlag("save-golden", false, "Write the final image of each test as its golden image.");


// True if text matches pattern. '*' matches any run of characters, and '?'
// matches one character.
static bool matchGlob(std::string_view pattern, std::string_view text) {
  size_t p = 0, t = 0;
  size_t star = std::string_view::npos, mark = 0;

  while (t < text.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
      p++;
      t++;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      mark = t;
    } else if (star != std::string_view::npos) {
      // Let the last '*' take one more character, and try again.
      p = star + 1;
      t = ++mark;
    } else {
      return false;
    }
  }

  while (p < pattern.size() && pattern[p] == '*') {
    p++;
  }
  return p == pattern.size();
}


// The smallest window. The overlay needs this much room, and the window shows
// a smaller render size larger, with the same aspect ratio.
static const glm::uvec2 kMinWindowSize{1280, 720};


// Run one test in a new Application. Exceptions from the Application fail the
// test; they do not stop the run.
static rentest::TestResult runTest(const rentest::TestInfo &info, const rentest::RunSettings &settings, size_t index, size_t count) {
  rentest::TestResult result;
  const u64 validationStart = ren::getValidationErrorCount();
  const size_t imagesStart = ren::Image::allImages().size();

  try {
    const glm::uvec2 windowSize = glm::max(info.config.size, kMinWindowSize);
    ren::Application app(info.name, windowSize, ren::makeBox<rentest::TestWindow>(info, result, settings, index, count));
    ren::Application::RunOptions options;
    options.fixedDeltaTime = info.config.deltaTime;
    app.run(options);
  } catch (const std::exception &error) {
    result.failures.push_back(fmt::format("Application: {}", error.what()));
  }

  // Every image of the test must be destroyed with its Application. An image
  // that lives on is a leak, or a cache that outlives the device.
  const size_t imagesEnd = ren::Image::allImages().size();
  if (imagesEnd > imagesStart) {
    result.failures.push_back(fmt::format("{} image(s) still exist after the Application closed", imagesEnd - imagesStart));
  }

  // The window checks at the end of the test. Errors after that, e.g. in the
  // interactive frames or at shutdown, are found here.
  const u64 errors = ren::getValidationErrorCount() - validationStart;
  if (errors > result.validationErrors) {
    result.failures.push_back(fmt::format("{} Vulkan validation error(s) after the test finished. The last: {}", errors - result.validationErrors,
                                          ren::getLastValidationError()));
  }

  return result;
}


// The terminal style of a verdict. The same as the log tags in Logging.cpp.
static fmt::text_style verdictStyle(rentest::TestResult::Verdict verdict) {
  switch (verdict) {
    case rentest::TestResult::Verdict::Pass:
      return fmt::emphasis::bold | fg(fmt::terminal_color::green);
    case rentest::TestResult::Verdict::Fail:
      return fmt::emphasis::bold | fg(fmt::terminal_color::red);
    case rentest::TestResult::Verdict::Skip:
      return fmt::emphasis::bold | fg(fmt::terminal_color::yellow);
    default:
      return {};
  }
}


// The results go to stdout directly, not through ren::println(), so that the
// color codes do not go into the log of the engine.
static void printResult(const rentest::TestInfo &info, const rentest::TestResult &result, size_t nameWidth) {
  auto verdict = result.verdict();
  fmt::print(verdictStyle(verdict), "[{}]", rentest::verdictName(verdict));
  fmt::print(" {:<{}}", info.name, nameWidth);
  if (result.golden) {
    const auto &golden = *result.golden;
    fmt::print("  golden: mean {:.6f}  max {:.3f}  ({} of {} pixels differ)", golden.meanError, golden.maxError, golden.differentPixels,
               golden.pixelCount);
  }
  if (result.goldenSaved) {
    fmt::print(fmt::emphasis::bold, "  golden saved");
  }
  if (result.manual != rentest::TestResult::Verdict::None) {
    auto automatic = result.automatic();
    fmt::print(" (automatic: ");
    fmt::print(verdictStyle(automatic), "{}", rentest::verdictName(automatic));
    fmt::print(")");
  }
  fmt::print("\n");

  for (auto &failure : result.failures) {
    fmt::print("    {}\n", failure);
  }
  if (result.skipped()) {
    fmt::print("    skipped: {}\n", result.skipReason);
  }
  if (!result.note.empty()) {
    fmt::print("    note: {}\n", result.note);
  }
}


// Print one count of the summary. A count of zero has no color.
static void printCount(size_t count, const char *label, rentest::TestResult::Verdict verdict) {
  auto style = count > 0 ? verdictStyle(verdict) : fmt::text_style{};
  fmt::print(style, "{} {}", count, label);
}


int main(int argc, char **argv) {
  ren::parseFlags(argc, argv);

  auto all = rentest::getTests();

  // The filter and the summary use names, so two tests with one name are an
  // error.
  std::set<std::string> names;
  for (auto *test : all) {
    if (!names.insert(test->name).second) {
      ren::errln("Two tests have the name '{}' (one is in {})", test->name, test->file);
      return 1;
    }
  }

  std::vector<const rentest::TestInfo *> tests;
  for (auto *test : all) {
    if (matchGlob(testFilter.get(), test->name)) {
      tests.push_back(test);
    }
  }

  if (listTests.get()) {
    for (auto *test : tests) {
      ren::println("{}  ({})", test->name, test->file);
    }
    return 0;
  }

  if (tests.empty()) {
    ren::errln("No test matches '{}'", testFilter.get());
    return 1;
  }


  // A rerun runs the same test again and drops its last result.
  std::vector<std::pair<const rentest::TestInfo *, rentest::TestResult>> results;
  bool aborted = false;

  rentest::RunSettings settings;
  settings.interactive = interactiveFlag.get();
  settings.saveGolden = saveGoldenFlag.get();

  // The width of the name column, so that the golden numbers line up.
  size_t nameWidth = 0;
  for (auto *test : tests) {
    nameWidth = std::max(nameWidth, test->name.size());
  }

  for (size_t i = 0; i < tests.size();) {
    auto result = runTest(*tests[i], settings, i, tests.size());
    if (result.rerun && !result.aborted) {
      continue;
    }

    printResult(*tests[i], result, nameWidth);
    results.emplace_back(tests[i], std::move(result));
    if (results.back().second.aborted) {
      aborted = true;
      break;
    }
    i++;
  }


  // Summary
  size_t passed = 0, failed = 0, skipped = 0;
  for (auto &[info, result] : results) {
    switch (result.verdict()) {
      case rentest::TestResult::Verdict::Pass:
        passed++;
        break;
      case rentest::TestResult::Verdict::Fail:
        failed++;
        break;
      default:
        skipped++;
        break;
    }
  }

  fmt::print("\n");
  printCount(passed, "passed", rentest::TestResult::Verdict::Pass);
  fmt::print(", ");
  printCount(failed, "failed", rentest::TestResult::Verdict::Fail);
  fmt::print(", ");
  printCount(skipped, "skipped", rentest::TestResult::Verdict::Skip);
  fmt::print("\n");
  if (aborted) {
    ren::errln("The run stopped after {} of {} tests", results.size(), tests.size());
  }
  return (failed > 0 || aborted) ? 1 : 0;
}
