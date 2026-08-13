#include <rstd/test/gtest.hpp>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using namespace rstd::literals;
using rstd::ffi::OsString;

namespace
{

struct TrackedValue {
    int* drops;

    explicit TrackedValue(int& drops): drops(&drops) {}
    TrackedValue(const TrackedValue&)            = delete;
    TrackedValue& operator=(const TrackedValue&) = delete;
    TrackedValue(TrackedValue&& other) noexcept: drops(other.drops) { other.drops = nullptr; }
    TrackedValue& operator=(TrackedValue&& other) noexcept {
        if (this != &other) {
            if (drops != nullptr) ++*drops;
            drops       = other.drops;
            other.drops = nullptr;
        }
        return *this;
    }
    ~TrackedValue() {
        if (drops != nullptr) ++*drops;
    }
};

template<typename... Tokens>
auto lifecycle_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(tokens)), ...);
    return values;
}

} // namespace

TEST(ArgparseLifecycle, MatchesOutlivesParserAndParserIsReusable) {
    auto            command = Command::make("tool"_str);
    auto            value   = command.add_arg(Arg<String>::value("value"_str, string_parser()));
    Option<Matches> saved   = None();
    {
        auto built = rstd::move(command).build();
        ASSERT_TRUE(built.is_ok());
        auto parser = rstd::move(built).unwrap();

        auto first = parser.parse_from(lifecycle_argv("tool"_str, "first"_str));
        ASSERT_TRUE(first.is_ok());
        auto first_outcome = rstd::move(first).unwrap();
        saved              = Some(rstd::move(first_outcome).as_Parsed().value);

        auto second = parser.parse_from(lifecycle_argv("tool"_str, "second"_str));
        ASSERT_TRUE(second.is_ok());
        auto second_outcome = rstd::move(second).unwrap();
        auto second_matches = rstd::move(second_outcome).as_Parsed().value;
        auto second_value   = second_matches.get_one(value);
        EXPECT_EQ(***second_value, "second"_str);
    }

    ASSERT_TRUE(saved.is_some());
    auto first_value = saved->get_one(value);
    ASSERT_TRUE(first_value.is_ok());
    EXPECT_EQ(***first_value, "first"_str);
}

TEST(ArgparseLifecycle, CommandKeyOutlivesParser) {
    auto child      = Command::make("run"_str);
    auto child_key  = child.key();
    auto copied_key = child_key;
    auto root       = Command::make("tool"_str);
    root.add_subcommand(rstd::move(child));

    Option<Matches> saved = None();
    {
        auto built = rstd::move(root).build();
        ASSERT_TRUE(built.is_ok());
        auto parser = rstd::move(built).unwrap();
        auto result = parser.parse_from(lifecycle_argv("tool"_str, "run"_str));
        ASSERT_TRUE(result.is_ok());
        auto outcome = rstd::move(result).unwrap();
        saved        = Some(rstd::move(outcome).as_Parsed().value);

        auto repeated = parser.parse_from(lifecycle_argv("tool"_str, "run"_str));
        ASSERT_TRUE(repeated.is_ok());
        auto repeated_outcome = rstd::move(repeated).unwrap();
        auto repeated_matches = rstd::move(repeated_outcome).as_Parsed().value;
        EXPECT_TRUE(repeated_matches.subcommand_matches(child_key).is_some());
    }

    ASSERT_TRUE(saved.is_some());
    EXPECT_TRUE(saved->subcommand_matches(copied_key).is_some());
}

TEST(ArgparseLifecycle, CleansProvisionalTypedValuesAfterValidationFailure) {
    int  drops   = 0;
    auto command = Command::make("tool"_str);
    command.add_arg(Arg<TrackedValue>::value(
        "value"_str,
        parse_with<TrackedValue>(
            [&drops](ref<rstd::ffi::OsStr>) -> Result<TrackedValue, ValueError> {
                return Ok(TrackedValue { drops });
            })));
    command.add_arg(Arg<bool>::flag("required"_str).long_name("required"_str).required());
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(lifecycle_argv("tool"_str, "value"_str));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(result.unwrap_err().is_MissingRequiredArgument());
    EXPECT_EQ(drops, 1);
}

TEST(ArgparseLifecycle, AcceptsRstdIntoIteratorInput) {
    auto command = Command::make("tool"_str);
    auto value   = command.add_arg(Arg<String>::value("value"_str, string_parser()));
    auto built   = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto input = rstd::array<OsString, 2> {
        OsString::from("tool"_str),
        OsString::from("array-value"_str),
    };
    auto result = parser.parse_from(rstd::move(input));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto matches = rstd::move(outcome).as_Parsed().value;
    auto parsed  = matches.get_one(value);
    ASSERT_TRUE(parsed.is_ok());
    EXPECT_EQ(***parsed, "array-value"_str);
}
