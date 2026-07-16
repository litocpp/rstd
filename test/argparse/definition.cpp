#include <gtest/gtest.h>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;

TEST(ArgparseDefinition, BuildsTypedSchemaAndIndexesOptions) {
    auto command = Command::make("server");
    command.about("Run the server");
    command.version("1.0");

    auto input = Arg<String>::value("input", string_parser());
    input.help("Input file");
    auto input_key = command.add_arg(rstd::move(input));

    auto verbose = Arg<u8>::count("verbose");
    verbose.short_name('v');
    verbose.long_name("verbose");
    auto verbose_key = command.add_arg(rstd::move(verbose));

    static_assert(rstd::mtp::same_as<decltype(input_key), ArgKey<String>>);
    static_assert(rstd::mtp::same_as<decltype(verbose_key), ArgKey<u8>>);

    auto result = rstd::move(command).build();
    ASSERT_TRUE(result.is_ok());
    auto parser = rstd::move(result).unwrap();
    EXPECT_EQ(parser.name(), "server");
    EXPECT_EQ(parser.arg_count(), 4u);
    EXPECT_EQ(parser.positional_count(), 1u);
    EXPECT_TRUE(parser.contains_arg("input"));
    EXPECT_TRUE(parser.contains_arg("help"));
    EXPECT_TRUE(parser.contains_arg("version"));
    EXPECT_TRUE(parser.contains_option("-v"));
    EXPECT_TRUE(parser.contains_option("--verbose"));
    EXPECT_TRUE(parser.contains_option("--help"));
}

TEST(ArgparseDefinition, RejectsDuplicateArgumentId) {
    auto command = Command::make("tool");
    command.disable_help();

    command.add_arg(Arg<bool>::flag("mode").long_name("first"));
    command.add_arg(Arg<bool>::flag("mode").long_name("second"));

    auto result = rstd::move(command).build();
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(result.unwrap_err().is_DuplicateArgumentId());
}

TEST(ArgparseDefinition, RejectsDuplicateOption) {
    auto command = Command::make("tool");
    command.disable_help();

    auto first = Arg<bool>::flag("first");
    first.long_name("same");
    command.add_arg(rstd::move(first));

    auto second = Arg<bool>::flag("second");
    second.long_name("same");
    command.add_arg(rstd::move(second));

    auto result = rstd::move(command).build();
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(result.unwrap_err().is_DuplicateOption());
}

TEST(ArgparseDefinition, RejectsInvalidNamesAndValueCounts) {
    {
        auto command = Command::make("");
        auto result  = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidCommandName());
    }
    {
        auto command = Command::make("tool");
        command.disable_help();
        auto arg = Arg<String>::value("value", string_parser());
        arg.long_name("not valid");
        command.add_arg(rstd::move(arg));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidLongName());
    }
    {
        auto command = Command::make("tool");
        command.disable_help();
        auto arg = Arg<String>::value("value", string_parser());
        arg.num_args(NumArgs::range(2, 1));
        command.add_arg(rstd::move(arg));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidValueCount());
    }
}

TEST(ArgparseDefinition, SupportsRvalueBuildersAndParsesDefaultsDuringBuild) {
    auto port_parser = [] {
        return parse_with<u16>([](ref<rstd::ffi::OsStr> value) -> Result<u16, ValueError> {
            auto text = value.to_str();
            if (text.is_some() && *text == "8080") return Ok(u16 { 8080 });
            return Err(ValueError::Message(String::make("invalid port")));
        });
    };

    auto command = Command::make("tool").about("about").disable_version();
    auto key     = command.add_arg(Arg<u16>::value("port", port_parser())
                                       .long_name("port")
                                       .help("Port")
                                       .default_value("8080"));
    static_assert(rstd::mtp::same_as<decltype(key), ArgKey<u16>>);

    auto result = rstd::move(command).build();
    EXPECT_TRUE(result.is_ok());

    auto invalid = Command::make("tool").disable_help();
    invalid.add_arg(Arg<u16>::value("port", port_parser()).default_value("not-a-number"));
    auto invalid_result = rstd::move(invalid).build();
    ASSERT_TRUE(invalid_result.is_err());
    EXPECT_TRUE(invalid_result.unwrap_err().is_InvalidDefaultValue());
}

TEST(ArgparseDefinition, RejectsBuiltinCollisionsAndInvalidImplicitValues) {
    {
        auto command = Command::make("tool");
        command.add_arg(Arg<bool>::flag("custom-help").long_name("help"));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_DuplicateOption());
    }
    {
        auto parser  = parse_with<String>([](ref<rstd::ffi::OsStr>) -> Result<String, ValueError> {
            return Err(ValueError::Message(String::make("invalid")));
        });
        auto command = Command::make("tool");
        command.add_arg(Arg<String>::value("value", rstd::move(parser))
                            .long_name("value")
                            .num_args(NumArgs::optional())
                            .implicit_value("bad"));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidImplicitValue());
    }
}

TEST(ArgparseDefinition, RejectsInvalidGroupsAndAmbiguousPositionals) {
    {
        auto command = Command::make("tool");
        auto value   = command.add_arg(Arg<bool>::flag("value").long_name("value"));
        command.add_group(ArgGroup::make("group").arg(value).arg(value));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidGroup());
    }
    {
        auto command = Command::make("tool");
        command.add_arg(Arg<String>::value("first", string_parser()).num_args(NumArgs::any()));
        command.add_arg(Arg<String>::value("second", string_parser()).num_args(NumArgs::any()));
        auto result = rstd::move(command).build();
        ASSERT_TRUE(result.is_err());
        EXPECT_TRUE(result.unwrap_err().is_InvalidValueCount());
    }
}
