#include <gtest/gtest.h>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using rstd::ffi::OsString;

template<typename... Tokens>
auto value_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(ref<str>(tokens))), ...);
    return values;
}

TEST(ArgparseValues, AppendsWithoutReparsingAndKeepsRawIndices) {
    usize parse_count = 0;
    auto  parser_fn   = parse_with<String>(
        [&parse_count](ref<rstd::ffi::OsStr> value) -> Result<String, ValueError> {
            ++parse_count;
            auto text = value.to_str();
            if (text.is_none()) return Err(ValueError::InvalidUtf8());
            return Ok(String::make(*text));
        });

    auto command = Command::make("tool");
    auto include = command.add_arg(
        Arg<String>::value("include", rstd::move(parser_fn)).short_name('I').append());
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();
    auto result = parser.parse_from(value_argv("tool", "-I", "first", "-Isecond"));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto matches = rstd::move(outcome).as_Parsed().value;
    EXPECT_EQ(parse_count, 2u);

    auto values = matches.get_many(include);
    ASSERT_TRUE(values.is_ok());
    ASSERT_TRUE(values->is_some());
    auto iterator = rstd::move(**values);
    EXPECT_EQ(**iterator.next(), "first");
    EXPECT_EQ(**iterator.next(), "second");
    EXPECT_TRUE(iterator.next().is_none());
    EXPECT_EQ(parse_count, 2u);

    ASSERT_TRUE(matches.raw_values("include").is_some());
    EXPECT_EQ(matches.raw_values("include")->len(), 2u);
    ASSERT_TRUE(matches.indices("include").is_some());
    EXPECT_EQ((*matches.indices("include"))[0], 2u);
    EXPECT_EQ((*matches.indices("include"))[1], 3u);
    ASSERT_TRUE(matches.occurrence_ends("include").is_some());
    EXPECT_EQ((*matches.occurrence_ends("include"))[0], 1u);
    EXPECT_EQ((*matches.occurrence_ends("include"))[1], 2u);
}

TEST(ArgparseValues, BuildsAndReusesImplicitAndDefaultValues) {
    usize parse_count = 0;
    auto  make_parser = [&parse_count] {
        return parse_with<String>(
            [&parse_count](ref<rstd::ffi::OsStr> value) -> Result<String, ValueError> {
                ++parse_count;
                auto text = value.to_str();
                if (text.is_none()) return Err(ValueError::InvalidUtf8());
                return Ok(String::make(*text));
            });
    };

    auto command = Command::make("tool");
    auto color   = command.add_arg(Arg<String>::value("color", make_parser())
                                       .long_name("color")
                                       .num_args(NumArgs::optional())
                                       .implicit_value("auto")
                                       .default_value("never"));
    auto built   = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    EXPECT_EQ(parse_count, 2u);
    auto parser = rstd::move(built).unwrap();

    auto implicit_result = parser.parse_from(value_argv("tool", "--color"));
    ASSERT_TRUE(implicit_result.is_ok());
    auto implicit_outcome = rstd::move(implicit_result).unwrap();
    auto implicit_matches = rstd::move(implicit_outcome).as_Parsed().value;
    auto implicit         = implicit_matches.get_one(color);
    EXPECT_EQ(***implicit, "auto");

    auto default_result = parser.parse_from(value_argv("tool"));
    ASSERT_TRUE(default_result.is_ok());
    auto default_outcome = rstd::move(default_result).unwrap();
    auto default_matches = rstd::move(default_outcome).as_Parsed().value;
    auto fallback        = default_matches.get_one(color);
    EXPECT_EQ(***fallback, "never");
    EXPECT_EQ(parse_count, 2u);
}

TEST(ArgparseValues, UsesExactShortAliasBeforeClusterAndSupportsHyphenValues) {
    auto command = Command::make("tool");
    auto exact   = command.add_arg(Arg<String>::value("exact", string_parser()).short_alias("abc"));
    auto a       = command.add_arg(Arg<bool>::flag("a").short_name('a'));
    auto b       = command.add_arg(Arg<bool>::flag("b").short_name('b'));
    auto c       = command.add_arg(Arg<bool>::flag("c").short_name('c'));
    auto negative = command.add_arg(
        Arg<String>::value("negative", string_parser()).long_name("number").allow_hyphen_values());
    auto disabled = command.add_arg(Arg<bool>::set_false("disabled").long_name("disabled"));
    auto built    = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result =
        parser.parse_from(value_argv("tool", "-abc", "value", "--number", "-12", "--disabled"));
    ASSERT_TRUE(result.is_ok());
    auto outcome        = rstd::move(result).unwrap();
    auto matches        = rstd::move(outcome).as_Parsed().value;
    auto exact_value    = matches.get_one(exact);
    auto negative_value = matches.get_one(negative);
    auto disabled_value = matches.get_one(disabled);
    EXPECT_EQ(***exact_value, "value");
    EXPECT_EQ(***negative_value, "-12");
    EXPECT_FALSE(***disabled_value);
    EXPECT_FALSE(matches.contains("a"));
    EXPECT_FALSE(matches.contains("b"));
    EXPECT_FALSE(matches.contains("c"));
    (void)a;
    (void)b;
    (void)c;
}

TEST(ArgparseValues, ValidatesChoices) {
    auto choices = Vec<String>::make();
    choices.push(String::make("fast"));
    choices.push(String::make("safe"));
    auto command = Command::make("tool");
    auto mode    = command.add_arg(
        Arg<String>::value("mode", choice_parser<String>(string_parser(), rstd::move(choices))));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    EXPECT_TRUE(parser.parse_from(value_argv("tool", "fast")).is_ok());
    auto invalid = parser.parse_from(value_argv("tool", "other"));
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().is_InvalidValue());
    (void)mode;
}

TEST(ArgparseValues, ReservesVariadicTokensForFollowingPositionals) {
    auto command = Command::make("tool");
    auto leading =
        command.add_arg(Arg<String>::value("leading", string_parser()).num_args(NumArgs::any()));
    auto tag      = command.add_arg(Arg<String>::value("tag", string_parser()).long_name("tag"));
    auto trailing = command.add_arg(Arg<String>::value("trailing", string_parser()).required());
    auto built    = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(value_argv("tool", "first", "--tag", "x", "last"));
    ASSERT_TRUE(result.is_ok());
    auto outcome        = rstd::move(result).unwrap();
    auto matches        = rstd::move(outcome).as_Parsed().value;
    auto leading_values = matches.get_many(leading);
    ASSERT_TRUE(leading_values.is_ok());
    ASSERT_TRUE(leading_values->is_some());
    auto iterator = rstd::move(**leading_values);
    EXPECT_EQ(**iterator.next(), "first");
    auto trailing_value = matches.get_one(trailing);
    auto tag_value      = matches.get_one(tag);
    EXPECT_EQ(***trailing_value, "last");
    EXPECT_EQ(***tag_value, "x");
}

TEST(ArgparseValues, RejectsIncompatibleAccessorsAndFormatsCustomErrors) {
    auto command  = Command::make("tool");
    auto repeated = command.add_arg(
        Arg<String>::value("repeated", string_parser()).long_name("repeated").append());
    command.add_arg(Arg<String>::value(
        "custom", parse_with<String>([](ref<rstd::ffi::OsStr>) -> Result<String, u8> {
            return Err(u8 { 7 });
        })));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto valid = parser.parse_from(value_argv("tool", "--repeated", "one", "value"));
    ASSERT_TRUE(valid.is_err());
    auto error = rstd::move(valid).unwrap_err();
    ASSERT_TRUE(error.is_InvalidValue());
    EXPECT_EQ(error.as_InvalidValue().error.as_Message().message, "7");

    auto only_repeated = Command::make("tool");
    auto key           = only_repeated.add_arg(
        Arg<String>::value("repeated", string_parser()).long_name("repeated").append());
    auto only_built = rstd::move(only_repeated).build();
    ASSERT_TRUE(only_built.is_ok());
    auto only_parser = rstd::move(only_built).unwrap();
    auto only_result = only_parser.parse_from(value_argv("tool", "--repeated", "one"));
    ASSERT_TRUE(only_result.is_ok());
    auto only_outcome = rstd::move(only_result).unwrap();
    auto matches      = rstd::move(only_outcome).as_Parsed().value;
    auto one          = matches.get_one(key);
    ASSERT_TRUE(one.is_err());
    EXPECT_TRUE(one.unwrap_err().is_IncompatibleAccessor());
    (void)repeated;
}
