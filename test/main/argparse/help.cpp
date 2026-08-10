#include <rstd/test/gtest.hpp>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using namespace rstd::literals;
using rstd::ffi::OsString;

template<typename... Tokens>
auto help_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(tokens)), ...);
    return values;
}

TEST(ArgparseHelp, RendersDeterministicSchemaOwnedHelpUsageAndVersion) {
    auto choices = Vec<String>::make();
    choices.push(String::make("fast"_str));
    choices.push(String::make("safe"_str));

    auto command = Command::make("tool"_str);
    command.about("Short description"_str);
    command.long_about("Long description"_str);
    command.version("1.0"_str);
    command.after_help("More details"_str);
    command.add_arg(Arg<String>::value("input"_str, string_parser())
                        .value_name("INPUT"_str)
                        .help("Input file"_str)
                        .required());
    command.add_arg(
        Arg<String>::value("mode"_str, choice_parser<String>(string_parser(), rstd::move(choices)))
            .long_name("mode"_str)
            .help("Execution mode"_str)
            .help_heading("Tuning"_str)
            .default_value("safe"_str));
    command.add_arg(Arg<bool>::flag("secret"_str).long_name("secret"_str).hidden());
    command.add_subcommand(Command::make("run"_str).about("Run it"_str));

    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    EXPECT_EQ(parser.render_usage(), "Usage: tool [OPTIONS] <INPUT> [COMMAND]"_str);
    EXPECT_EQ(parser.render_version(), "tool 1.0"_str);
    EXPECT_EQ(parser.render_help(),
              "Long description\n"_str
              "\n"_str
              "Usage: tool [OPTIONS] <INPUT> [COMMAND]\n"_str
              "\n"_str
              "Arguments:\n"_str
              "  INPUT\tInput file\n"_str
              "\n"_str
              "Options:\n"_str
              "  -h, --help\tPrint help\n"_str
              "  -V, --version\tPrint version\n"_str
              "\n"_str
              "Tuning:\n"_str
              "  --mode\tExecution mode [default: safe] [possible values: fast, safe]\n"_str
              "\n"_str
              "Subcommands:\n"_str
              "  run\tRun it\n"_str
              "\n"_str
              "More details\n"_str);
}

TEST(ArgparseHelp, RendersParseErrorsAsStderrMetadataWithoutIo) {
    auto command = Command::make("tool"_str);
    auto built   = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();
    auto result = parser.parse_from(help_argv("tool"_str, "--bad"_str));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    EXPECT_EQ(error.kind(), ParseError::Tag::UnknownArgument);

    auto report = parser.render_error(error);
    EXPECT_EQ(report.target(), OutputTarget::Tag::Stderr);
    EXPECT_EQ(report.exit_code(), i32(2));
    EXPECT_EQ(report.text(),
              "error: unknown argument '--bad'\n"_str
              "\n"_str
              "Usage: tool [OPTIONS]\n"_str);
}
