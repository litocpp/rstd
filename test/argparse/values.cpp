#include <rstd/test/gtest.hpp>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using namespace rstd::literals;
using rstd::ffi::OsString;

template<typename... Tokens>
auto value_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(tokens)), ...);
    return values;
}

TEST(ArgparseValues, AppendsWithoutReparsingAndKeepsRawIndices) {
    usize parse_count {};
    auto  parser_fn = parse_with<String>(
        [&parse_count](ref<rstd::ffi::OsStr> value) -> Result<String, ValueError> {
            ++parse_count;
            auto text = value.to_str();
            if (text.is_none()) return Err(ValueError::InvalidUtf8());
            return Ok(String::make(*text));
        });

    auto command = Command::make("tool"_str);
    auto include = command.add_arg(
        Arg<String>::value("include"_str, rstd::move(parser_fn)).short_name(u8('I')).append());
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();
    auto result = parser.parse_from(value_argv("tool"_str, "-I"_str, "first"_str, "-Isecond"_str));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto matches = rstd::move(outcome).as_Parsed().value;
    EXPECT_EQ(parse_count, usize(2));

    auto values = matches.get_many(include);
    ASSERT_TRUE(values.is_ok());
    ASSERT_TRUE(values->is_some());
    auto iterator = rstd::move(**values);
    EXPECT_EQ(**iterator.next(), "first"_str);
    EXPECT_EQ(**iterator.next(), "second"_str);
    EXPECT_TRUE(iterator.next().is_none());
    EXPECT_EQ(parse_count, usize(2));

    ASSERT_TRUE(matches.raw_values("include"_str).is_some());
    EXPECT_EQ(matches.raw_values("include"_str)->len(), usize(2));
    ASSERT_TRUE(matches.indices("include"_str).is_some());
    EXPECT_EQ((*matches.indices("include"_str))[usize()], usize(2));
    EXPECT_EQ((*matches.indices("include"_str))[usize(1)], usize(3));
    ASSERT_TRUE(matches.occurrence_ends("include"_str).is_some());
    EXPECT_EQ((*matches.occurrence_ends("include"_str))[usize()], usize(1));
    EXPECT_EQ((*matches.occurrence_ends("include"_str))[usize(1)], usize(2));
}

TEST(ArgparseValues, BuildsAndReusesImplicitAndDefaultValues) {
    usize parse_count {};
    auto  make_parser = [&parse_count] {
        return parse_with<String>(
            [&parse_count](ref<rstd::ffi::OsStr> value) -> Result<String, ValueError> {
                ++parse_count;
                auto text = value.to_str();
                if (text.is_none()) return Err(ValueError::InvalidUtf8());
                return Ok(String::make(*text));
            });
    };

    auto command = Command::make("tool"_str);
    auto color   = command.add_arg(Arg<String>::value("color"_str, make_parser())
                                       .long_name("color"_str)
                                       .num_args(NumArgs::optional())
                                       .implicit_value("auto"_str)
                                       .default_value("never"_str));
    auto built   = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    EXPECT_EQ(parse_count, usize(2));
    auto parser = rstd::move(built).unwrap();

    auto implicit_result = parser.parse_from(value_argv("tool"_str, "--color"_str));
    ASSERT_TRUE(implicit_result.is_ok());
    auto implicit_outcome = rstd::move(implicit_result).unwrap();
    auto implicit_matches = rstd::move(implicit_outcome).as_Parsed().value;
    auto implicit         = implicit_matches.get_one(color);
    EXPECT_EQ(***implicit, "auto"_str);

    auto default_result = parser.parse_from(value_argv("tool"_str));
    ASSERT_TRUE(default_result.is_ok());
    auto default_outcome = rstd::move(default_result).unwrap();
    auto default_matches = rstd::move(default_outcome).as_Parsed().value;
    auto fallback        = default_matches.get_one(color);
    EXPECT_EQ(***fallback, "never"_str);
    EXPECT_EQ(parse_count, usize(2));
}

TEST(ArgparseValues, UsesExactShortAliasBeforeClusterAndSupportsHyphenValues) {
    auto command = Command::make("tool"_str);
    auto exact =
        command.add_arg(Arg<String>::value("exact"_str, string_parser()).short_alias("abc"_str));
    auto a        = command.add_arg(Arg<bool>::flag("a"_str).short_name(u8('a')));
    auto b        = command.add_arg(Arg<bool>::flag("b"_str).short_name(u8('b')));
    auto c        = command.add_arg(Arg<bool>::flag("c"_str).short_name(u8('c')));
    auto negative = command.add_arg(Arg<String>::value("negative"_str, string_parser())
                                        .long_name("number"_str)
                                        .allow_hyphen_values());
    auto disabled = command.add_arg(Arg<bool>::set_false("disabled"_str).long_name("disabled"_str));
    auto built    = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(value_argv(
        "tool"_str, "-abc"_str, "value"_str, "--number"_str, "-12"_str, "--disabled"_str));
    ASSERT_TRUE(result.is_ok());
    auto outcome        = rstd::move(result).unwrap();
    auto matches        = rstd::move(outcome).as_Parsed().value;
    auto exact_value    = matches.get_one(exact);
    auto negative_value = matches.get_one(negative);
    auto disabled_value = matches.get_one(disabled);
    EXPECT_EQ(***exact_value, "value"_str);
    EXPECT_EQ(***negative_value, "-12"_str);
    EXPECT_FALSE(***disabled_value);
    EXPECT_FALSE(matches.contains("a"_str));
    EXPECT_FALSE(matches.contains("b"_str));
    EXPECT_FALSE(matches.contains("c"_str));
    (void)a;
    (void)b;
    (void)c;
}

TEST(ArgparseValues, ValidatesChoices) {
    auto choices = Vec<String>::make();
    choices.push(String::make("fast"_str));
    choices.push(String::make("safe"_str));
    auto command = Command::make("tool"_str);
    auto mode    = command.add_arg(Arg<String>::value(
        "mode"_str, choice_parser<String>(string_parser(), rstd::move(choices))));
    auto built   = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    EXPECT_TRUE(parser.parse_from(value_argv("tool"_str, "fast"_str)).is_ok());
    auto invalid = parser.parse_from(value_argv("tool"_str, "other"_str));
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().is_InvalidValue());
    (void)mode;
}

TEST(ArgparseValues, ReservesVariadicTokensForFollowingPositionals) {
    auto command = Command::make("tool"_str);
    auto leading = command.add_arg(
        Arg<String>::value("leading"_str, string_parser()).num_args(NumArgs::any()));
    auto tag = command.add_arg(Arg<String>::value("tag"_str, string_parser()).long_name("tag"_str));
    auto trailing = command.add_arg(Arg<String>::value("trailing"_str, string_parser()).required());
    auto built    = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result =
        parser.parse_from(value_argv("tool"_str, "first"_str, "--tag"_str, "x"_str, "last"_str));
    ASSERT_TRUE(result.is_ok());
    auto outcome        = rstd::move(result).unwrap();
    auto matches        = rstd::move(outcome).as_Parsed().value;
    auto leading_values = matches.get_many(leading);
    ASSERT_TRUE(leading_values.is_ok());
    ASSERT_TRUE(leading_values->is_some());
    auto iterator = rstd::move(**leading_values);
    EXPECT_EQ(**iterator.next(), "first"_str);
    auto trailing_value = matches.get_one(trailing);
    auto tag_value      = matches.get_one(tag);
    EXPECT_EQ(***trailing_value, "last"_str);
    EXPECT_EQ(***tag_value, "x"_str);
}

TEST(ArgparseValues, RejectsIncompatibleAccessorsAndFormatsCustomErrors) {
    auto command  = Command::make("tool"_str);
    auto repeated = command.add_arg(
        Arg<String>::value("repeated"_str, string_parser()).long_name("repeated"_str).append());
    command.add_arg(Arg<String>::value(
        "custom"_str, parse_with<String>([](ref<rstd::ffi::OsStr>) -> Result<String, u8> {
            return Err(u8(7));
        })));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto valid =
        parser.parse_from(value_argv("tool"_str, "--repeated"_str, "one"_str, "value"_str));
    ASSERT_TRUE(valid.is_err());
    auto error = rstd::move(valid).unwrap_err();
    ASSERT_TRUE(error.is_InvalidValue());
    EXPECT_EQ(error.as_InvalidValue().error.as_Message().message, "7"_str);

    auto only_repeated = Command::make("tool"_str);
    auto key           = only_repeated.add_arg(
        Arg<String>::value("repeated"_str, string_parser()).long_name("repeated"_str).append());
    auto only_built = rstd::move(only_repeated).build();
    ASSERT_TRUE(only_built.is_ok());
    auto only_parser = rstd::move(only_built).unwrap();
    auto only_result = only_parser.parse_from(value_argv("tool"_str, "--repeated"_str, "one"_str));
    ASSERT_TRUE(only_result.is_ok());
    auto only_outcome = rstd::move(only_result).unwrap();
    auto matches      = rstd::move(only_outcome).as_Parsed().value;
    auto one          = matches.get_one(key);
    ASSERT_TRUE(one.is_err());
    EXPECT_TRUE(one.unwrap_err().is_IncompatibleAccessor());
    (void)repeated;
}
