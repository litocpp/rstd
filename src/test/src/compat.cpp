#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace rstd::test::gtest
{

Descriptor* registry_head {};

struct DeathState {
    String        program;
    Option<String> case_name;
    Option<usize>  index;
};

DeathState death_state;

auto raw_str(const char* value, unsigned long length) noexcept -> ref<str> {
    return ref<str>::from_raw_parts_unchecked(
        reinterpret_cast<const byte*>(value), usize(length));
}

auto raw_str(const char* value) noexcept -> ref<str> {
    return raw_str(value, __builtin_strlen(value));
}

auto message_text(const Message& message, const char* fallback) -> String {
    if (message.length() == 0) return String::make(raw_str(fallback));
    return String::make(raw_str(message.data(), message.length()));
}

template<typename T>
auto append_formatted(Message& message, T value) -> void {
    auto rendered = rstd::format("{}", value);
    message.append(reinterpret_cast<const char*>(rendered.as_str().data()),
                   rendered.as_str().size().to_primitive());
}

auto output_contains(const Vec<u8>& output, ref<str> pattern) noexcept -> bool {
    if (pattern.is_empty()) return true;
    if (pattern.len() > output.len()) return false;
    auto limit = output.len() - pattern.len();
    for (usize offset {}; offset <= limit; ++offset) {
        auto matches = true;
        for (usize index {}; index < pattern.len(); ++index) {
            if (output[offset + index] != pattern[index]) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

} // namespace rstd::test::gtest

rstd::test::gtest::Registrar::Registrar(Descriptor* descriptor) noexcept {
    auto* head = __atomic_load_n(&registry_head, __ATOMIC_RELAXED);
    do {
        descriptor->next = head;
    } while (!__atomic_compare_exchange_n(
        &registry_head, &head, descriptor, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
}

auto rstd::test::gtest::registered_head() noexcept -> Descriptor* {
    return __atomic_load_n(&registry_head, __ATOMIC_ACQUIRE);
}

auto rstd::test::gtest::Message::append(const char* value, unsigned long length) noexcept -> void {
    if (value == nullptr || length == 0 || length_ == sizeof(bytes_) - 1) return;
    auto available = sizeof(bytes_) - 1 - length_;
    if (length > available) length = available;
    for (unsigned long index = 0; index < length; ++index) bytes_[length_ + index] = value[index];
    length_ += length;
    bytes_[length_] = '\0';
}

auto rstd::test::gtest::Message::operator<<(const char* value) noexcept -> Message& {
    if (value != nullptr) append(value, __builtin_strlen(value));
    return *this;
}

auto rstd::test::gtest::Message::operator<<(char value) noexcept -> Message& {
    append(&value, 1);
    return *this;
}

auto rstd::test::gtest::Message::operator<<(bool value) noexcept -> Message& {
    append(value ? "true" : "false", value ? 4 : 5);
    return *this;
}

auto rstd::test::gtest::Message::operator<<(int value) noexcept -> Message& {
    append_formatted(*this, value);
    return *this;
}

auto rstd::test::gtest::Message::operator<<(unsigned int value) noexcept -> Message& {
    append_formatted(*this, value);
    return *this;
}

auto rstd::test::gtest::Message::operator<<(long value) noexcept -> Message& {
    append_formatted(*this, value);
    return *this;
}

auto rstd::test::gtest::Message::operator<<(unsigned long value) noexcept -> Message& {
    append_formatted(*this, value);
    return *this;
}

auto rstd::test::gtest::Message::operator<<(long long value) noexcept -> Message& {
    append_formatted(*this, value);
    return *this;
}

auto rstd::test::gtest::Message::operator<<(unsigned long long value) noexcept -> Message& {
    append_formatted(*this, value);
    return *this;
}

auto rstd::test::gtest::Message::operator<<(float value) noexcept -> Message& {
    (void)value;
    append("<float>", 7);
    return *this;
}

auto rstd::test::gtest::Message::operator<<(double value) noexcept -> Message& {
    append_formatted(*this, value);
    return *this;
}

auto rstd::test::gtest::ResultHelper::operator=(const Message& message) const noexcept -> void {
    auto text = message_text(message, fallback_);
    if (kind_ == ResultKind::Skip) {
        skip_current(text.as_str(), file_, line_);
    } else {
        fail_current(text.as_str(), file_, line_, true);
    }
}

auto rstd::test::gtest::record_assertion(bool        success,
                                         bool        fatal,
                                         const char* expression,
                                         const char* file,
                                         int         line) noexcept -> void {
    if (success) return;
    fail_current(raw_str(expression), file, line, fatal);
}

auto rstd::test::gtest::float_equal(double left,
                                    double right,
                                    bool   single_precision) noexcept -> bool {
    if (left == right) return true;
    auto difference = __builtin_fabs(left - right);
    auto magnitude  = __builtin_fmax(__builtin_fabs(left), __builtin_fabs(right));
    auto epsilon    = single_precision ? 1.1920928955078125e-7 : 2.2204460492503131e-16;
    return difference <= epsilon * 4.0 * __builtin_fmax(1.0, magnitude);
}

auto rstd::test::gtest::float_near(double left,
                                   double right,
                                   double absolute_error) noexcept -> bool {
    return absolute_error >= 0.0 && __builtin_fabs(left - right) <= absolute_error;
}

[[noreturn]] auto rstd::test::gtest::death_survived() noexcept -> void {
    process::exit(i32(86));
}

namespace rstd::test::gtest
{

auto configure_death(const char*   program,
                     unsigned long program_length,
                     const char*   case_name,
                     unsigned long case_length,
                     bool          has_case,
                     unsigned long index) noexcept -> void {
    death_state = DeathState {
        .program = String::make(raw_str(program, program_length)),
        .case_name = has_case ? Some(String::make(raw_str(case_name, case_length))) : None(),
        .index = has_case ? Some(usize(index)) : None(),
    };
}

auto death_child_active() noexcept -> bool {
    return death_state.case_name.is_some() && death_state.index.is_some();
}

[[noreturn]] auto finish_death_child() noexcept -> void {
    process::exit(i32(87));
}

} // namespace rstd::test::gtest

auto rstd::test::gtest::death_begin(const char* pattern, const char* file, int line) noexcept
    -> DeathDecision {
    auto* context = current_test_context();
    if (context == nullptr) {
        fail_current("death assertion has no active test context"_str, file, line, false);
        return DeathDecision::Skip;
    }

    auto ordinal = context->next_death_index();
    if (death_child_active()) {
        auto current_name = context->full_name();
        if (current_name != death_state.case_name->as_str()) return DeathDecision::Skip;
        if (ordinal == *death_state.index) return DeathDecision::Execute;
        return DeathDecision::Skip;
    }

    auto command = process::Command::make(death_state.program.as_str());
    auto name    = context->full_name();
    command.arg("--rstd-test-death-case"_str)
        .arg(name.as_str())
        .arg("--rstd-test-death-index"_str)
        .arg(rstd::format("{}", ordinal).as_str())
        .set_stdout(process::Stdio::piped())
        .set_stderr(process::Stdio::piped());
    auto spawned = command.spawn();
    if (spawned.is_err()) {
        fail_current("failed to spawn death-test child"_str, file, line, false);
        return DeathDecision::Skip;
    }
    auto child   = rstd::move(spawned).unwrap();
    auto started = time::Instant::now();
    while (true) {
        auto waited = child.try_wait();
        if (waited.is_err()) {
            (void)child.kill();
            (void)child.wait();
            fail_current("failed to wait for death-test child"_str, file, line, false);
            return DeathDecision::Skip;
        }
        if (waited->is_some()) break;
        if (started.elapsed().as_millis() >= u64(10000)) {
            (void)child.kill();
            (void)child.wait();
            fail_current("death-test child timed out"_str, file, line, false);
            return DeathDecision::Skip;
        }
        thread::sleep(time::Duration::from_millis(u64(1)));
    }

    auto captured = child.wait_with_output();
    if (captured.is_err()) {
        fail_current("failed to collect death-test child output"_str, file, line, false);
        return DeathDecision::Skip;
    }
    auto output = rstd::move(captured).unwrap();
    auto code   = output.status.code();
    auto died   = !output.status.success() &&
                (code.is_none() || (*code != i32(86) && *code != i32(87)));
    if (!died) {
        fail_current("death statement returned normally"_str, file, line, false);
        return DeathDecision::Skip;
    }
    auto expected = raw_str(pattern);
    if (!output_contains(output.stderr_buf, expected)) {
        fail_current("death output did not contain expected text"_str, file, line, false);
    }
    return DeathDecision::Skip;
}
