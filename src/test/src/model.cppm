export module rstd.test:model;

import rstd;

using namespace rstd::prelude;

export namespace rstd::test
{

class TestContext;
thread_local TestContext* active_test_context {};

class TestContext {
    ref<str>       suite_;
    ref<str>       case_;
    usize          failures_ {};
    usize          death_index_ {};
    bool           fatal_ {};
    bool           skipped_ {};
    bool           report_ { true };
    Option<String> skip_message_;

public:
    TestContext(ref<str> suite, ref<str> case_name, bool report = true)
        : suite_(suite), case_(case_name), report_(report) {}

    auto expect(bool condition, ref<str> message) -> bool {
        if (condition) return true;
        ++failures_;
        if (report_) io::eprintln("[failure] {}.{}: {}", suite_, case_, message);
        return false;
    }

    auto fail(ref<str> message) -> void { (void)expect(false, message); }

    auto fail_at(ref<str> message, const char* file, int line, bool fatal) -> void {
        ++failures_;
        fatal_         = fatal_ || fatal;
        auto file_text = ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(file),
                                                            usize(__builtin_strlen(file)));
        if (report_)
            io::eprintln("[failure] {}.{} {}:{}: {}", suite_, case_, file_text, line, message);
    }

    auto skip_at(ref<str> message, const char* file, int line) -> void {
        if (failures_ != usize {}) return;
        skipped_       = true;
        skip_message_  = Some(String::make(message));
        auto file_text = ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(file),
                                                            usize(__builtin_strlen(file)));
        if (report_)
            io::println("[skipped] {}.{} {}:{}: {}", suite_, case_, file_text, line, message);
    }

    auto failures() const noexcept -> usize { return failures_; }
    auto fatal() const noexcept -> bool { return fatal_; }
    auto skipped() const noexcept -> bool { return skipped_; }
    auto skip_message() const noexcept -> Option<ref<str>> {
        if (skip_message_.is_none()) return None();
        return Some(skip_message_->as_str());
    }
    auto full_name() const -> String { return rstd::format("{}.{}", suite_, case_); }
    auto next_death_index() noexcept -> usize {
        auto current = death_index_;
        ++death_index_;
        return current;
    }
};

auto current_test_context() noexcept -> TestContext*;
auto replace_test_context(TestContext* context) noexcept -> TestContext*;
auto fail_current(ref<str> message, const char* file, int line, bool fatal) noexcept -> void;
auto skip_current(ref<str> message, const char* file, int line) noexcept -> void;

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

auto rstd::test::current_test_context() noexcept -> TestContext* {
    return active_test_context;
}

auto rstd::test::replace_test_context(TestContext* context) noexcept -> TestContext* {
    auto* previous      = active_test_context;
    active_test_context = context;
    return previous;
}

auto rstd::test::fail_current(ref<str> message, const char* file, int line, bool fatal) noexcept
    -> void {
    auto* context = current_test_context();
    if (context == nullptr) {
        io::eprintln("[failure] no active test: {}", message);
        return;
    }
    context->fail_at(message, file, line, fatal);
}

auto rstd::test::skip_current(ref<str> message, const char* file, int line) noexcept -> void {
    auto* context = current_test_context();
    if (context == nullptr) {
        io::eprintln("[failure] no active test: {}", message);
        return;
    }
    context->skip_at(message, file, line);
}
