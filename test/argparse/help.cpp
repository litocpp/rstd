#include <gtest/gtest.h>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using rstd::ffi::OsString;

template<typename... Tokens>
auto help_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(ref<str>(tokens))), ...);
    return values;
}

TEST(ArgparseHelp, RendersDeterministicSchemaOwnedHelpUsageAndVersion) {
    auto choices = Vec<String>::make();
    choices.push(String::make("fast"));
    choices.push(String::make("safe"));

    auto command = Command::make("tool");
    command.about("Short description");
    command.long_about("Long description");
    command.version("1.0");
    command.after_help("More details");
    command.add_arg(Arg<String>::value("input", string_parser())
                        .value_name("INPUT")
                        .help("Input file")
                        .required());
    command.add_arg(
        Arg<String>::value("mode", choice_parser<String>(string_parser(), rstd::move(choices)))
            .long_name("mode")
            .help("Execution mode")
            .help_heading("Tuning")
            .default_value("safe"));
    command.add_arg(Arg<bool>::flag("secret").long_name("secret").hidden());
    command.add_subcommand(Command::make("run").about("Run it"));

    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    EXPECT_EQ(parser.render_usage(), "Usage: tool [OPTIONS] <INPUT> [COMMAND]");
    EXPECT_EQ(parser.render_version(), "tool 1.0");
    EXPECT_EQ(parser.render_help(),
              "Long description\n"
              "\n"
              "Usage: tool [OPTIONS] <INPUT> [COMMAND]\n"
              "\n"
              "Arguments:\n"
              "  INPUT\tInput file\n"
              "\n"
              "Options:\n"
              "  -h, --help\tPrint help\n"
              "  -V, --version\tPrint version\n"
              "\n"
              "Tuning:\n"
              "  --mode\tExecution mode [default: safe] [possible values: fast, safe]\n"
              "\n"
              "Subcommands:\n"
              "  run\tRun it\n"
              "\n"
              "More details\n");
}

TEST(ArgparseHelp, RendersParseErrorsAsStderrMetadataWithoutIo) {
    auto command = Command::make("tool");
    auto built   = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();
    auto result = parser.parse_from(help_argv("tool", "--bad"));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    EXPECT_EQ(error.kind(), ParseError::Tag::UnknownArgument);

    auto report = parser.render_error(error);
    EXPECT_EQ(report.target(), OutputTarget::Tag::Stderr);
    EXPECT_EQ(report.exit_code(), 2);
    EXPECT_EQ(report.text(),
              "error: unknown argument '--bad'\n"
              "\n"
              "Usage: tool [OPTIONS]\n");
}
