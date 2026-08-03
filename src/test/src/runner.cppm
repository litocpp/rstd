module;
#include <rstd/test/gtest.hpp>

export module rstd.test:runner;

import rstd;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace rstd::test
{

struct RunOptions {
    String         program;
    Option<String> filter;
    Option<String> death_case;
    Option<usize>  death_index;
    bool           list {};
};

auto parse_index(ref<str> value) -> Result<usize, String> {
    if (value.is_empty()) return Err(String::make("death index must not be empty"_str));
    usize result {};
    for (auto byte : value.as_bytes()) {
        auto raw = byte.to_primitive();
        if (raw < '0' || raw > '9') return Err(String::make("death index must be decimal"_str));
        result = result * usize(10) + usize(raw - '0');
    }
    return Ok(result);
}

auto parse_options() -> Result<RunOptions, String> {
    auto arguments = env::args();
    auto program   = arguments.next();
    if (program.is_none()) return Err(String::make("test executable path is unavailable"_str));
    auto options = RunOptions { .program = rstd::move(program).unwrap() };
    for (auto argument = arguments.next(); argument.is_some(); argument = arguments.next()) {
        if (argument->as_str() == "--list"_str) {
            options.list = true;
            continue;
        }
        if (argument->as_str() == "--filter"_str) {
            auto value = arguments.next();
            if (value.is_none()) return Err(String::make("--filter requires a value"_str));
            if (options.filter.is_some()) {
                return Err(String::make("--filter may only be provided once"_str));
            }
            options.filter = Some(rstd::move(value).unwrap());
            continue;
        }
        if (argument->as_str() == "--rstd-test-death-case"_str) {
            auto value = arguments.next();
            if (value.is_none()) {
                return Err(String::make("--rstd-test-death-case requires a value"_str));
            }
            options.death_case = Some(rstd::move(value).unwrap());
            continue;
        }
        if (argument->as_str() == "--rstd-test-death-index"_str) {
            auto value = arguments.next();
            if (value.is_none()) {
                return Err(String::make("--rstd-test-death-index requires a value"_str));
            }
            auto parsed = parse_index(value->as_str());
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
            options.death_index = Some(rstd::move(parsed).unwrap());
            continue;
        }
        return Err(rstd::format("unknown test argument '{}'", argument->as_str()));
    }
    if (options.death_case.is_some() != options.death_index.is_some()) {
        return Err(String::make("death case and index must be provided together"_str));
    }
    return Ok(rstd::move(options));
}

auto full_name(ref<str> suite, ref<str> case_name) -> String {
    return rstd::format("{}.{}", suite, case_name);
}

auto full_name(const TestSuite& suite, const TestCase& test_case) -> String {
    return full_name(suite.name, test_case.name);
}

auto validate_suites(slice<TestSuite> suites) -> Result<empty, String> {
    for (usize left_suite {}; left_suite < suites.len(); ++left_suite) {
        const auto& left = suites[left_suite];
        if (left.name.is_empty()) return Err(String::make("test suite name must not be empty"_str));
        for (usize left_case {}; left_case < left.cases.len(); ++left_case) {
            const auto& candidate = left.cases[left_case];
            if (candidate.name.is_empty()) {
                return Err(rstd::format("test case in suite '{}' has an empty name", left.name));
            }
            if (candidate.function == nullptr) {
                return Err(
                    rstd::format("test case '{}.{}' has no function", left.name, candidate.name));
            }
            auto candidate_name = full_name(left, candidate);
            for (usize right_suite {}; right_suite <= left_suite; ++right_suite) {
                const auto& right      = suites[right_suite];
                auto        case_limit = right_suite == left_suite ? left_case : right.cases.len();
                for (usize right_case {}; right_case < case_limit; ++right_case) {
                    if (full_name(right, right.cases[right_case]) == candidate_name.as_str()) {
                        return Err(
                            rstd::format("duplicate test case '{}'", candidate_name.as_str()));
                    }
                }
            }
        }
    }
    return Ok(empty {});
}

auto raw_str(const char* value) noexcept -> ref<str> {
    return ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(value),
                                              usize(__builtin_strlen(value)));
}

auto descriptor_name(const gtest::Descriptor& descriptor) -> String {
    return full_name(raw_str(descriptor.suite), raw_str(descriptor.name));
}

auto collect_registered() -> Result<Vec<gtest::Descriptor*>, String> {
    auto result = Vec<gtest::Descriptor*>::make();
    for (auto* descriptor = gtest::registered_head(); descriptor != nullptr;
         descriptor       = descriptor->next) {
        if (descriptor->suite == nullptr || descriptor->suite[0] == '\0') {
            return Err(String::make("registered test suite name must not be empty"_str));
        }
        if (descriptor->name == nullptr || descriptor->name[0] == '\0') {
            return Err(String::make("registered test case name must not be empty"_str));
        }
        if (descriptor->function == nullptr) {
            return Err(String::make("registered test case has no function"_str));
        }
        result.push(rstd::move(descriptor));
    }
    rstd::slice_::sort_unstable_by(
        result.as_mut_slice().as_mut_ref(),
        [](const gtest::Descriptor* left, const gtest::Descriptor* right) {
            auto left_name  = descriptor_name(*left);
            auto right_name = descriptor_name(*right);
            return left_name < right_name;
        });
    for (usize index = usize(1); index < result.len(); ++index) {
        auto previous = descriptor_name(*result[index - usize(1)]);
        auto current  = descriptor_name(*result[index]);
        if (previous == current.as_str()) {
            return Err(rstd::format("duplicate test case '{}'", current.as_str()));
        }
    }
    return Ok(rstd::move(result));
}

auto selected(const RunOptions& options, ref<str> name) -> bool {
    if (options.death_case.is_some()) return name == options.death_case->as_str();
    return options.filter.is_none() || name.contains(options.filter->as_str());
}

struct RunCounts {
    usize executed {};
    usize passed {};
    usize failed {};
    usize skipped {};
};

auto record_result(ref<str> name, const TestContext& context, RunCounts& counts) -> void {
    if (context.failures() != usize {}) {
        ++counts.failed;
        io::eprintln("[failed] {}: {} failures", name, context.failures());
    } else if (context.skipped()) {
        ++counts.skipped;
    } else {
        ++counts.passed;
        io::println("[ok] {}", name);
    }
}

auto finish_run(const RunOptions& options, const RunCounts& counts) -> i32 {
    if (gtest::death_child_active()) gtest::finish_death_child();
    if (counts.executed == usize {}) {
        io::eprintln("rstd.test: no tests matched");
        return i32(2);
    }
    if (options.list) return i32 {};
    io::println("test result: {}. {} passed; {} failed; {} skipped",
                counts.failed == usize {} ? "ok"_str : "failed"_str,
                counts.passed,
                counts.failed,
                counts.skipped);
    return counts.failed == usize {} ? i32 {} : i32(1);
}

auto prepare_run() -> Result<RunOptions, String> {
    auto parsed = parse_options();
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    auto options     = rstd::move(parsed).unwrap();
    auto case_name   = options.death_case.is_some() ? options.death_case->as_str() : ref<str> {};
    auto death_index = options.death_index.is_some() ? *options.death_index : usize {};
    gtest::configure_death(reinterpret_cast<const char*>(options.program.as_str().data()),
                           options.program.as_str().len().to_primitive(),
                           reinterpret_cast<const char*>(case_name.data()),
                           case_name.len().to_primitive(),
                           options.death_case.is_some(),
                           death_index.to_primitive());
    return Ok(rstd::move(options));
}

} // namespace rstd::test

export namespace rstd::test
{

auto run(slice<TestSuite> suites) -> i32 {
    auto valid = validate_suites(suites);
    if (valid.is_err()) {
        io::eprintln("rstd.test: {}", rstd::move(valid).unwrap_err());
        return i32(2);
    }
    auto prepared = prepare_run();
    if (prepared.is_err()) {
        io::eprintln("rstd.test: {}", rstd::move(prepared).unwrap_err());
        return i32(2);
    }
    auto options = rstd::move(prepared).unwrap();
    auto counts  = RunCounts {};
    for (const auto& suite : suites) {
        for (const auto& test_case : suite.cases) {
            auto name = full_name(suite, test_case);
            if (! selected(options, name.as_str())) continue;
            ++counts.executed;
            if (options.list) {
                io::println("{}", name.as_str());
                continue;
            }

            io::println("[run] {}", name.as_str());
            auto context  = TestContext(suite.name, test_case.name);
            auto previous = replace_test_context(rstd::addressof(context));
            test_case.function(context);
            (void)replace_test_context(previous);
            record_result(name.as_str(), context, counts);
        }
    }
    return finish_run(options, counts);
}

auto run_registered() -> i32 {
    auto collected = collect_registered();
    if (collected.is_err()) {
        io::eprintln("rstd.test: {}", rstd::move(collected).unwrap_err());
        return i32(2);
    }
    auto prepared = prepare_run();
    if (prepared.is_err()) {
        io::eprintln("rstd.test: {}", rstd::move(prepared).unwrap_err());
        return i32(2);
    }
    auto descriptors = rstd::move(collected).unwrap();
    auto options     = rstd::move(prepared).unwrap();
    auto counts      = RunCounts {};
    for (const auto* descriptor : descriptors) {
        auto name = descriptor_name(*descriptor);
        if (! selected(options, name.as_str())) continue;
        ++counts.executed;
        if (options.list) {
            io::println("{}", name.as_str());
            continue;
        }

        io::println("[run] {}", name.as_str());
        auto context  = TestContext(raw_str(descriptor->suite), raw_str(descriptor->name));
        auto previous = replace_test_context(rstd::addressof(context));
        descriptor->function();
        (void)replace_test_context(previous);
        record_result(name.as_str(), context, counts);
    }
    return finish_run(options, counts);
}

} // namespace rstd::test
