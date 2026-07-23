#include <gtest/gtest.h>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using namespace rstd::literals;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

template<typename... Tokens>
auto error_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(tokens)), ...);
    return values;
}

TEST(ArgparseErrors, DistinguishesMissingTooFewTooManyAndDuplicate) {
    auto command = Command::make("tool"_str);
    command.add_arg(Arg<String>::value("pair"_str, string_parser())
                        .long_name("pair"_str)
                        .num_args(NumArgs::exact(usize(2))));
    command.add_arg(Arg<bool>::flag("flag"_str).long_name("flag"_str));
    command.add_arg(Arg<String>::value("once"_str, string_parser()).long_name("once"_str));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto missing = parser.parse_from(error_argv("tool"_str, "--pair"_str, "--flag"_str));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().is_MissingValue());

    auto too_few = parser.parse_from(error_argv("tool"_str, "--pair"_str, "one"_str));
    ASSERT_TRUE(too_few.is_err());
    EXPECT_TRUE(too_few.unwrap_err().is_TooFewValues());

    auto too_many = parser.parse_from(error_argv("tool"_str, "--flag=value"_str));
    ASSERT_TRUE(too_many.is_err());
    EXPECT_TRUE(too_many.unwrap_err().is_TooManyValues());

    auto duplicate = parser.parse_from(error_argv("tool"_str, "--once"_str, "one"_str, "--once"_str, "two"_str));
    ASSERT_TRUE(duplicate.is_err());
    EXPECT_TRUE(duplicate.unwrap_err().is_DuplicateArgument());
}

TEST(ArgparseErrors, ReportsNonUtf8SeparatelyWithValueIndex) {
    auto command = Command::make("tool"_str);
    command.add_arg(Arg<String>::value("text"_str, string_parser()));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    rstd::byte bytes[] = { rstd::byte { 0xFF } };
    auto       args    = error_argv("tool"_str);
    auto       encoded = slice<u8>::from_raw_parts(bytes, usize(1));
    args.push(OsString::from(ref<OsStr>::from_encoded_bytes_unchecked(encoded)));
    auto result = parser.parse_from(rstd::move(args));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    ASSERT_TRUE(error.is_InvalidUtf8Value());
    EXPECT_EQ(error.as_InvalidUtf8Value().index, usize(1));
    EXPECT_EQ(error.as_InvalidUtf8Value().value.as_os_str().as_encoded_bytes()[usize()],
              u8(0xFF));
}

TEST(ArgparseErrors, KeepsGlobalIndicesAcrossSubcommands) {
    auto run = Command::make("run"_str);
    run.add_arg(Arg<String>::value("value"_str, string_parser()).long_name("value"_str));
    auto root = Command::make("tool"_str);
    root.add_subcommand(rstd::move(run));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(error_argv("tool"_str, "run"_str, "--unknown"_str));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    ASSERT_TRUE(error.is_UnknownArgument());
    EXPECT_EQ(error.as_UnknownArgument().index, usize(2));
    EXPECT_EQ(error.command_path(), "tool run"_str);
    EXPECT_EQ(error.usage(), "Usage: tool run [OPTIONS]"_str);
}

TEST(ArgparseErrors, SuggestsNearbySchemaOwnedOptions) {
    auto command = Command::make("tool"_str);
    command.version("1.0"_str);
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(error_argv("tool"_str, "--verison"_str));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    ASSERT_TRUE(error.is_UnknownArgument());
    ASSERT_TRUE(error.as_UnknownArgument().suggestion.is_some());
    EXPECT_EQ(*error.as_UnknownArgument().suggestion, "--version"_str);
}
