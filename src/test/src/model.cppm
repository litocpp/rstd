export module rstd.test:model;

import rstd;

using namespace rstd::prelude;

export namespace rstd::test
{

class TestContext {
    ref<str> suite_;
    ref<str> case_;
    usize    failures_ {};

public:
    TestContext(ref<str> suite, ref<str> case_name): suite_(suite), case_(case_name) {}

    auto expect(bool condition, ref<str> message) -> bool {
        if (condition) return true;
        ++failures_;
        io::eprintln("[failure] {}.{}: {}", suite_, case_, message);
        return false;
    }

    auto fail(ref<str> message) -> void { (void)expect(false, message); }

    auto failures() const noexcept -> usize { return failures_; }
};

using TestFunction = void (*)(TestContext&) noexcept;

struct TestCase {
    ref<str>     name;
    TestFunction function {};
};

struct TestSuite {
    ref<str>        name;
    slice<TestCase> cases;
};

} // namespace rstd::test
