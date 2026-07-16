#include <gtest/gtest.h>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

template<typename... Tokens>
auto argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(ref<str>(tokens))), ...);
    return values;
}

TEST(ArgparseParsing, ParsesOptionsPositionalsClustersAndAttachedValues) {
    auto command = Command::make("tool");
    auto input   = command.add_arg(
        Arg<String>::value("input", string_parser()).value_name("INPUT").required());
    auto output = command.add_arg(
        Arg<String>::value("output", string_parser()).short_name('o').long_name("output"));
    auto verbose = command.add_arg(Arg<u8>::count("verbose").short_name('v'));

    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();
    auto parsed = parser.parse_from(argv("tool", "-vvoresult", "source"));
    ASSERT_TRUE(parsed.is_ok());
    auto outcome = rstd::move(parsed).unwrap();
    ASSERT_TRUE(outcome.is_Parsed());
    auto matches = rstd::move(outcome).as_Parsed().value;

    auto input_value = matches.get_one(input);
    ASSERT_TRUE(input_value.is_ok());
    ASSERT_TRUE(input_value->is_some());
    EXPECT_EQ(***input_value, "source");

    auto output_value = matches.get_one(output);
    ASSERT_TRUE(output_value.is_ok());
    ASSERT_TRUE(output_value->is_some());
    EXPECT_EQ(***output_value, "result");

    auto count = matches.get_one(verbose);
    ASSERT_TRUE(count.is_ok());
    ASSERT_TRUE(count->is_some());
    EXPECT_EQ(***count, 2u);
    EXPECT_EQ(matches.occurrences("verbose"), 2u);
}

TEST(ArgparseParsing, SupportsLongEqualsEndOfOptionsAndDefaults) {
    auto command = Command::make("tool");
    auto mode    = command.add_arg(
        Arg<String>::value("mode", string_parser()).long_name("mode").default_value("safe"));
    auto input = command.add_arg(Arg<String>::value("input", string_parser()));

    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto explicit_result = parser.parse_from(argv("tool", "--mode=fast", "--", "-input"));
    ASSERT_TRUE(explicit_result.is_ok());
    auto explicit_outcome = rstd::move(explicit_result).unwrap();
    auto explicit_matches = rstd::move(explicit_outcome).as_Parsed().value;
    auto explicit_mode    = explicit_matches.get_one(mode);
    auto explicit_input   = explicit_matches.get_one(input);
    EXPECT_EQ(***explicit_mode, "fast");
    EXPECT_EQ(***explicit_input, "-input");
    EXPECT_TRUE(explicit_matches.value_source("mode")->is_CommandLine());

    auto default_result = parser.parse_from(argv("tool", "file"));
    ASSERT_TRUE(default_result.is_ok());
    auto default_outcome = rstd::move(default_result).unwrap();
    auto default_matches = rstd::move(default_outcome).as_Parsed().value;
    auto default_mode    = default_matches.get_one(mode);
    EXPECT_EQ(***default_mode, "safe");
    EXPECT_TRUE(default_matches.value_source("mode")->is_DefaultValue());
}

TEST(ArgparseParsing, PreservesNonUtf8Values) {
    auto command = Command::make("tool");
    auto value   = command.add_arg(Arg<OsString>::value("value", os_string_parser()));
    auto built   = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    u8   bytes[] = { 0xFF, 'x' };
    auto args    = argv("tool");
    args.push(OsString::from(ref<OsStr>::from_raw_parts(bytes, 2)));
    auto result = parser.parse_from(rstd::move(args));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto matches = rstd::move(outcome).as_Parsed().value;
    auto parsed  = matches.get_one(value);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(parsed->is_some());
    EXPECT_EQ((***parsed).as_os_str().data()[0], 0xFF);
}

TEST(ArgparseParsing, KnownParsingKeepsUnknownClusterAtomic) {
    auto command = Command::make("tool");
    auto verbose = command.add_arg(Arg<u8>::count("verbose").short_name('v'));
    auto built   = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_known_from(argv("tool", "-vx", "--other=value"));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto known   = rstd::move(outcome).as_Parsed().value;
    EXPECT_FALSE(known.matches()->contains("verbose"));
    ASSERT_EQ(known.unknown().len(), 2u);
    EXPECT_EQ(known.unknown()[0].as_os_str().to_str(), Some(ref<str>("-vx")));
    EXPECT_EQ(known.unknown()[1].as_os_str().to_str(), Some(ref<str>("--other=value")));

    auto foreign_command = Command::make("other");
    auto foreign         = foreign_command.add_arg(Arg<bool>::flag("foreign"));
    auto access          = known.matches()->get_one(foreign);
    ASSERT_TRUE(access.is_err());
    EXPECT_TRUE(access.unwrap_err().is_ForeignKey());

    auto absent = known.matches()->get_one(verbose);
    ASSERT_TRUE(absent.is_ok());
    EXPECT_TRUE(absent->is_none());
}

TEST(ArgparseParsing, ReturnsStructuredErrorsAndDisplayRequests) {
    auto command = Command::make("tool");
    command.version("1.2.3");
    auto required = command.add_arg(
        Arg<String>::value("required", string_parser()).long_name("required").required());
    (void)required;
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto unknown = parser.parse_from(argv("tool", "--unknown"));
    ASSERT_TRUE(unknown.is_err());
    EXPECT_TRUE(unknown.unwrap_err().is_UnknownArgument());

    auto missing = parser.parse_from(argv("tool"));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().is_MissingRequiredArgument());

    auto help = parser.parse_from(argv("tool", "--help"));
    ASSERT_TRUE(help.is_ok());
    ASSERT_TRUE(help->is_Display());
    EXPECT_EQ(help->as_Display().request.kind(), DisplayKind::Tag::Help);
    EXPECT_EQ(help->as_Display().request.target(), OutputTarget::Tag::Stdout);
    EXPECT_EQ(help->as_Display().request.exit_code(), 0);

    auto version = parser.parse_from(argv("tool", "--version"));
    ASSERT_TRUE(version.is_ok());
    ASSERT_TRUE(version->is_Display());
    EXPECT_EQ(version->as_Display().request.text(), "tool 1.2.3");
}
