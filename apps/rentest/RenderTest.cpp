#include "RenderTest.h"

#include <algorithm>

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
