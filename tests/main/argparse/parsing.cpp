#include <rstd/test/gtest.hpp>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using namespace rstd::literals;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

template<typename... Tokens>
auto argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(tokens)), ...);
    return values;
}

TEST(ArgparseParsing, ParsesOptionsPositionalsClustersAndAttachedValues) {
    auto command = Command::make("tool"_str);
    auto input   = command.add_arg(
        Arg<String>::value("input"_str, string_parser()).value_name("INPUT"_str).required());
    auto output  = command.add_arg(Arg<String>::value("output"_str, string_parser())
                                       .short_name(u8('o'))
                                       .long_name("output"_str));
    auto verbose = command.add_arg(Arg<u8>::count("verbose"_str).short_name(u8('v')));

    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();
    auto parsed = parser.parse_from(argv("tool"_str, "-vvoresult"_str, "source"_str));
    ASSERT_TRUE(parsed.is_ok());
    auto outcome = rstd::move(parsed).unwrap();
    ASSERT_TRUE(outcome.is_Parsed());
    auto matches = rstd::move(outcome).as_Parsed().value;

    auto input_value = matches.get_one(input);
    ASSERT_TRUE(input_value.is_ok());
    ASSERT_TRUE(input_value->is_some());
    EXPECT_EQ(***input_value, "source"_str);

    auto output_value = matches.get_one(output);
    ASSERT_TRUE(output_value.is_ok());
    ASSERT_TRUE(output_value->is_some());
    EXPECT_EQ(***output_value, "result"_str);

    auto count = matches.get_one(verbose);
    ASSERT_TRUE(count.is_ok());
    ASSERT_TRUE(count->is_some());
    EXPECT_EQ(***count, u8(2));
    EXPECT_EQ(matches.occurrences("verbose"_str), usize(2));
}

TEST(ArgparseParsing, SupportsLongEqualsEndOfOptionsAndDefaults) {
    auto command = Command::make("tool"_str);
    auto mode    = command.add_arg(Arg<String>::value("mode"_str, string_parser())
                                       .long_name("mode"_str)
                                       .default_value("safe"_str));
    auto input   = command.add_arg(Arg<String>::value("input"_str, string_parser()));

    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto explicit_result =
        parser.parse_from(argv("tool"_str, "--mode=fast"_str, "--"_str, "-input"_str));
    ASSERT_TRUE(explicit_result.is_ok());
    auto explicit_outcome = rstd::move(explicit_result).unwrap();
    auto explicit_matches = rstd::move(explicit_outcome).as_Parsed().value;
    auto explicit_mode    = explicit_matches.get_one(mode);
    auto explicit_input   = explicit_matches.get_one(input);
    EXPECT_EQ(***explicit_mode, "fast"_str);
    EXPECT_EQ(***explicit_input, "-input"_str);
    EXPECT_TRUE(explicit_matches.value_source("mode"_str)->is_CommandLine());

    auto default_result = parser.parse_from(argv("tool"_str, "file"_str));
    ASSERT_TRUE(default_result.is_ok());
    auto default_outcome = rstd::move(default_result).unwrap();
    auto default_matches = rstd::move(default_outcome).as_Parsed().value;
    auto default_mode    = default_matches.get_one(mode);
    EXPECT_EQ(***default_mode, "safe"_str);
    EXPECT_TRUE(default_matches.value_source("mode"_str)->is_DefaultValue());
}

TEST(ArgparseParsing, SupportsDottedLongOptions) {
    auto command  = Command::make("tool"_str);
    auto compiler = command.add_arg(
        Arg<String>::value("toolchain-cxx"_str, string_parser()).long_name("toolchain.cxx"_str));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parsed = built->parse_from(argv("tool"_str, "--toolchain.cxx=clang++-22"_str));
    ASSERT_TRUE(parsed.is_ok());
    auto matches = rstd::move(parsed).unwrap().as_Parsed().value;
    auto value   = matches.get_one(compiler);
    ASSERT_TRUE(value.is_ok());
    ASSERT_TRUE(value->is_some());
    EXPECT_EQ(***value, "clang++-22"_str);
}

TEST(ArgparseParsing, PreservesNonUtf8Values) {
    auto command = Command::make("tool"_str);
    auto value   = command.add_arg(Arg<OsString>::value("value"_str, os_string_parser()));
    auto built   = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    rstd::byte bytes[] = { rstd::byte { 0xFF }, rstd::byte { 0x78 } };
    auto       args    = argv("tool"_str);
    auto       encoded = slice<u8>::from_raw_parts(bytes, usize(2));
    args.push(OsString::from(ref<OsStr>::from_encoded_bytes_unchecked(encoded)));
    auto result = parser.parse_from(rstd::move(args));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto matches = rstd::move(outcome).as_Parsed().value;
    auto parsed  = matches.get_one(value);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(parsed->is_some());
    EXPECT_EQ((***parsed).as_os_str().as_encoded_bytes()[usize()], u8(0xFF));
}

TEST(ArgparseParsing, KnownParsingKeepsUnknownClusterAtomic) {
    auto command = Command::make("tool"_str);
    auto verbose = command.add_arg(Arg<u8>::count("verbose"_str).short_name(u8('v')));
    auto built   = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_known_from(argv("tool"_str, "-vx"_str, "--other=value"_str));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto known   = rstd::move(outcome).as_Parsed().value;
    EXPECT_FALSE(known.matches()->contains("verbose"_str));
    ASSERT_EQ(known.unknown().len(), usize(2));
    EXPECT_EQ(known.unknown()[usize()].as_os_str().to_str(), Some("-vx"_str));
    EXPECT_EQ(known.unknown()[usize(1)].as_os_str().to_str(), Some("--other=value"_str));

    auto foreign_command = Command::make("other"_str);
    auto foreign         = foreign_command.add_arg(Arg<bool>::flag("foreign"_str));
    auto access          = known.matches()->get_one(foreign);
    ASSERT_TRUE(access.is_err());
    EXPECT_TRUE(access.unwrap_err().is_ForeignKey());

    auto absent = known.matches()->get_one(verbose);
    ASSERT_TRUE(absent.is_ok());
    EXPECT_TRUE(absent->is_none());
}

TEST(ArgparseParsing, ReturnsStructuredErrorsAndDisplayRequests) {
    auto command = Command::make("tool"_str);
    command.version("1.2.3"_str);
    auto required = command.add_arg(
        Arg<String>::value("required"_str, string_parser()).long_name("required"_str).required());
    (void)required;
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto unknown = parser.parse_from(argv("tool"_str, "--unknown"_str));
    ASSERT_TRUE(unknown.is_err());
    EXPECT_TRUE(unknown.unwrap_err().is_UnknownArgument());

    auto missing = parser.parse_from(argv("tool"_str));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().is_MissingRequiredArgument());

    auto help = parser.parse_from(argv("tool"_str, "--help"_str));
    ASSERT_TRUE(help.is_ok());
    ASSERT_TRUE(help->is_Display());
    EXPECT_EQ(help->as_Display().request.kind(), DisplayKind::Tag::Help);
    EXPECT_EQ(help->as_Display().request.target(), OutputTarget::Tag::Stdout);
    EXPECT_EQ(help->as_Display().request.exit_code(), i32());

    auto version = parser.parse_from(argv("tool"_str, "--version"_str));
    ASSERT_TRUE(version.is_ok());
    ASSERT_TRUE(version->is_Display());
    EXPECT_EQ(version->as_Display().request.text(), "tool 1.2.3"_str);
}
