#include <gtest/gtest.h>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using namespace rstd::literals;

TEST(ArgparseDefinition, BuildsTypedSchemaAndIndexesOptions) {
    auto command = Command::make("server"_str);
    command.about("Run the server"_str);
    command.version("1.0"_str);

    auto input = Arg<String>::value("input"_str, string_parser());
    input.help("Input file"_str);
    auto input_key = command.add_arg(rstd::move(input));

    auto verbose = Arg<u8>::count("verbose"_str);
    verbose.short_name(u8('v'));
    verbose.long_name("verbose"_str);
    auto verbose_key = command.add_arg(rstd::move(verbose));

    static_assert(rstd::mtp::same_as<decltype(input_key), ArgKey<String>>);
    static_assert(rstd::mtp::same_as<decltype(verbose_key), ArgKey<u8>>);

    auto result = rstd::move(command).build();
    ASSERT_TRUE(result.is_ok());
    auto parser = rstd::move(result).unwrap();
    EXPECT_EQ(parser.name(), "server"_str);
    EXPECT_EQ(parser.arg_count(), usize(4));
    EXPECT_EQ(parser.positional_count(), usize(1));
    EXPECT_TRUE(parser.contains_arg("input"_str));
    EXPECT_TRUE(parser.contains_arg("help"_str));
    EXPECT_TRUE(parser.contains_arg("version"_str));
    EXPECT_TRUE(parser.contains_option("-v"_str));
    EXPECT_TRUE(parser.contains_option("--verbose"_str));
    EXPECT_TRUE(parser.contains_option("--help"_str));
}

TEST(ArgparseDefinition, RejectsDuplicateArgumentId) {
    auto command = Command::make("tool"_str);
    command.disable_help();

    command.add_arg(Arg<bool>::flag("mode"_str).long_name("first"_str));
    command.add_arg(Arg<bool>::flag("mode"_str).long_name("second"_str));

    auto result = rstd::move(command).build();
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(result.unwrap_err().is_DuplicateArgumentId());
}

TEST(ArgparseDefinition, RejectsDuplicateOption) {
    auto command = Command::make("tool"_str);
    command.disable_help();

    auto first = Arg<bool>::flag("first"_str);
    first.long_name("same"_str);
    command.add_arg(rstd::move(first));

    auto second = Arg<bool>::flag("second"_str);
    second.long_name("same"_str);
    command.add_arg(rstd::move(second));

    auto result = rstd::move(command).build();
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(result.unwrap_err().is_DuplicateOption());
}

TEST(ArgparseDefinition, RejectsInvalidNamesAndValueCounts) {
    {
        auto command = Command::make(""_str);
        auto result  = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidCommandName());
    }
    {
        auto command = Command::make("tool"_str);
        command.disable_help();
        auto arg = Arg<String>::value("value"_str, string_parser());
        arg.long_name("not valid"_str);
        command.add_arg(rstd::move(arg));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidLongName());
    }
    {
        auto command = Command::make("tool"_str);
        command.disable_help();
        auto arg = Arg<String>::value("value"_str, string_parser());
        arg.num_args(NumArgs::range(usize(2), usize(1)));
        command.add_arg(rstd::move(arg));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidValueCount());
    }
}

TEST(ArgparseDefinition, SupportsRvalueBuildersAndParsesDefaultsDuringBuild) {
    auto command = Command::make("tool"_str).about("about"_str).disable_version();
    auto key     = command.add_arg(Arg<u16>::value("port"_str, from_str_parser<u16>())
                                       .long_name("port"_str)
                                       .help("Port"_str)
                                       .default_value("8080"_str));
    static_assert(rstd::mtp::same_as<decltype(key), ArgKey<u16>>);

    auto result = rstd::move(command).build();
    EXPECT_TRUE(result.is_ok());

    auto invalid = Command::make("tool"_str).disable_help();
    invalid.add_arg(
        Arg<u16>::value("port"_str, from_str_parser<u16>()).default_value("not-a-number"_str));
    auto invalid_result = rstd::move(invalid).build();
    ASSERT_TRUE(invalid_result.is_err());
    auto error = rstd::move(invalid_result).unwrap_err();
    ASSERT_TRUE(error.is_InvalidDefaultValue());
    EXPECT_EQ(error.as_InvalidDefaultValue().error.as_Message().message,
              "invalid digit found in string"_str);
}

TEST(ArgparseDefinition, RejectsBuiltinCollisionsAndInvalidImplicitValues) {
    {
        auto command = Command::make("tool"_str);
        command.add_arg(Arg<bool>::flag("custom-help"_str).long_name("help"_str));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_DuplicateOption());
    }
    {
        auto parser  = parse_with<String>([](ref<rstd::ffi::OsStr>) -> Result<String, ValueError> {
            return Err(ValueError::Message(String::make("invalid"_str)));
        });
        auto command = Command::make("tool"_str);
        command.add_arg(Arg<String>::value("value"_str, rstd::move(parser))
                            .long_name("value"_str)
                            .num_args(NumArgs::optional())
                            .implicit_value("bad"_str));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidImplicitValue());
    }
}

TEST(ArgparseDefinition, RejectsInvalidGroupsAndAmbiguousPositionals) {
    {
        auto command = Command::make("tool"_str);
        auto value = command.add_arg(Arg<bool>::flag("value"_str).long_name("value"_str));
        command.add_group(ArgGroup::make("group"_str).arg(value).arg(value));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidGroup());
    }
    {
        auto command = Command::make("tool"_str);
        command.add_arg(
            Arg<String>::value("first"_str, string_parser()).num_args(NumArgs::any()));
        command.add_arg(
            Arg<String>::value("second"_str, string_parser()).num_args(NumArgs::any()));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidValueCount());
    }
}
