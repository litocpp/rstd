#include <gtest/gtest.h>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

template<typename... Tokens>
auto error_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(ref<str>(tokens))), ...);
    return values;
}

TEST(ArgparseErrors, DistinguishesMissingTooFewTooManyAndDuplicate) {
    auto command = Command::make("tool");
    command.add_arg(
        Arg<String>::value("pair", string_parser()).long_name("pair").num_args(NumArgs::exact(2)));
    command.add_arg(Arg<bool>::flag("flag").long_name("flag"));
    command.add_arg(Arg<String>::value("once", string_parser()).long_name("once"));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto missing = parser.parse_from(error_argv("tool", "--pair", "--flag"));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().is_MissingValue());

    auto too_few = parser.parse_from(error_argv("tool", "--pair", "one"));
    ASSERT_TRUE(too_few.is_err());
    EXPECT_TRUE(too_few.unwrap_err().is_TooFewValues());

    auto too_many = parser.parse_from(error_argv("tool", "--flag=value"));
    ASSERT_TRUE(too_many.is_err());
    EXPECT_TRUE(too_many.unwrap_err().is_TooManyValues());

    auto duplicate = parser.parse_from(error_argv("tool", "--once", "one", "--once", "two"));
    ASSERT_TRUE(duplicate.is_err());
    EXPECT_TRUE(duplicate.unwrap_err().is_DuplicateArgument());
}

TEST(ArgparseErrors, ReportsNonUtf8SeparatelyWithValueIndex) {
    auto command = Command::make("tool");
    command.add_arg(Arg<String>::value("text", string_parser()));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    u8   byte[] = { 0xFF };
    auto args   = error_argv("tool");
    args.push(OsString::from(ref<OsStr>::from_raw_parts(byte, 1)));
    auto result = parser.parse_from(rstd::move(args));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    ASSERT_TRUE(error.is_InvalidUtf8Value());
    EXPECT_EQ(error.as_InvalidUtf8Value().index, 1u);
    EXPECT_EQ(error.as_InvalidUtf8Value().value.as_os_str().data()[0], 0xFF);
}

TEST(ArgparseErrors, KeepsGlobalIndicesAcrossSubcommands) {
    auto run = Command::make("run");
    run.add_arg(Arg<String>::value("value", string_parser()).long_name("value"));
    auto root = Command::make("tool");
    root.add_subcommand(rstd::move(run));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(error_argv("tool", "run", "--unknown"));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    ASSERT_TRUE(error.is_UnknownArgument());
    EXPECT_EQ(error.as_UnknownArgument().index, 2u);
    EXPECT_EQ(error.command_path(), "tool run");
    EXPECT_EQ(error.usage(), "Usage: tool run [OPTIONS]");
}

TEST(ArgparseErrors, SuggestsNearbySchemaOwnedOptions) {
    auto command = Command::make("tool");
    command.version("1.0");
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(error_argv("tool", "--verison"));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    ASSERT_TRUE(error.is_UnknownArgument());
    ASSERT_TRUE(error.as_UnknownArgument().suggestion.is_some());
    EXPECT_EQ(*error.as_UnknownArgument().suggestion, "--version");
}
