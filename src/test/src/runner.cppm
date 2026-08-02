export module rstd.test:runner;

import rstd;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace rstd::test
{

struct RunOptions {
    Option<String> filter;
    bool           list {};
};

auto parse_options() -> Result<RunOptions, String> {
    auto arguments = env::args();
    (void)arguments.next();
    auto options = RunOptions {};
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
        return Err(rstd::format("unknown test argument '{}'", argument->as_str()));
    }
    return Ok(rstd::move(options));
}

auto full_name(const TestSuite& suite, const TestCase& test_case) -> String {
    return rstd::format("{}.{}", suite.name, test_case.name);
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

auto selected(const RunOptions& options, ref<str> name) -> bool {
    return options.filter.is_none() || name.contains(options.filter->as_str());
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
    auto parsed = parse_options();
    if (parsed.is_err()) {
        io::eprintln("rstd.test: {}", rstd::move(parsed).unwrap_err());
        return i32(2);
    }
    auto options = rstd::move(parsed).unwrap();

    usize executed {};
    usize passed {};
    usize failed {};
    for (const auto& suite : suites) {
        for (const auto& test_case : suite.cases) {
            auto name = full_name(suite, test_case);
            if (! selected(options, name.as_str())) continue;
            ++executed;
            if (options.list) {
                io::println("{}", name.as_str());
                continue;
            }

            io::println("[run] {}", name.as_str());
            auto context = TestContext(suite.name, test_case.name);
            test_case.function(context);
            if (context.failures() == usize {}) {
                ++passed;
                io::println("[ok] {}", name.as_str());
            } else {
                ++failed;
                io::eprintln("[failed] {}: {} failures", name.as_str(), context.failures());
            }
        }
    }

    if (executed == usize {}) {
        io::eprintln("rstd.test: no tests matched");
        return i32(2);
    }
    if (options.list) return i32 {};
    io::println("test result: {}. {} passed; {} failed",
                failed == usize {} ? "ok"_str : "failed"_str,
                passed,
                failed);
    return failed == usize {} ? i32 {} : i32(1);
}

} // namespace rstd::test
