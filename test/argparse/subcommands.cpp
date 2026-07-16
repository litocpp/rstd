#include <gtest/gtest.h>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using rstd::ffi::OsString;

template<typename... Tokens>
auto subcommand_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(ref<str>(tokens))), ...);
    return values;
}

TEST(ArgparseSubcommands, OwnsSchemaAndReturnsRecursiveMatches) {
    auto serve = Command::make("serve");
    serve.about("Run the server");
    serve.alias("s");
    auto port =
        serve.add_arg(Arg<String>::value("port", string_parser()).long_name("port").required());

    auto root = Command::make("tool");
    root.require_subcommand();
    root.add_subcommand(rstd::move(serve));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(subcommand_argv("tool", "s", "--port", "8080"));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto matches = rstd::move(outcome).as_Parsed().value;
    auto child   = matches.subcommand_matches("serve");
    ASSERT_TRUE(child.is_some());
    auto value = (*child)->get_one(port);
    ASSERT_TRUE(value.is_ok());
    ASSERT_TRUE(value->is_some());
    EXPECT_EQ(***value, "8080");
    ASSERT_TRUE(matches.subcommand().is_some());
    EXPECT_EQ(matches.subcommand()->get<0>(), "serve");
}

TEST(ArgparseSubcommands, ValidatesRequiredAndInvalidSubcommands) {
    auto root = Command::make("tool");
    root.require_subcommand();
    root.add_subcommand(Command::make("run"));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto missing = parser.parse_from(subcommand_argv("tool"));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().is_MissingSubcommand());

    auto invalid = parser.parse_from(subcommand_argv("tool", "other"));
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().is_InvalidSubcommand());

    auto help = parser.parse_from(subcommand_argv("tool", "run", "--help"));
    ASSERT_TRUE(help.is_ok());
    ASSERT_TRUE(help->is_Display());
    EXPECT_EQ(help->as_Display().request.kind(), DisplayKind::Tag::Help);
}

TEST(ArgparseSubcommands, EndOfOptionsForcesSameNameIntoPositional) {
    auto root  = Command::make("tool");
    auto value = root.add_arg(Arg<String>::value("value", string_parser()));
    root.add_subcommand(Command::make("run"));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(subcommand_argv("tool", "--", "run"));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto matches = rstd::move(outcome).as_Parsed().value;
    EXPECT_TRUE(matches.subcommand().is_none());
    auto parsed = matches.get_one(value);
    ASSERT_TRUE(parsed.is_ok());
    EXPECT_EQ(***parsed, "run");
}

TEST(ArgparseSubcommands, KnownParsingUnifiesParentAndChildUnknownTokens) {
    auto root = Command::make("tool");
    root.add_subcommand(Command::make("run"));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_known_from(
        subcommand_argv("tool", "--parent-unknown", "run", "--child-unknown"));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto known   = rstd::move(outcome).as_Parsed().value;
    ASSERT_EQ(known.unknown().len(), 2u);
    EXPECT_EQ(known.unknown()[0].as_os_str().to_str(), Some(ref<str>("--parent-unknown")));
    EXPECT_EQ(known.unknown()[1].as_os_str().to_str(), Some(ref<str>("--child-unknown")));
}

TEST(ArgparseSubcommands, RejectsDuplicateNamesAndAliases) {
    auto duplicate = Command::make("tool");
    duplicate.add_subcommand(Command::make("run"));
    duplicate.add_subcommand(Command::make("run"));
    auto duplicate_result = rstd::move(duplicate).build();
    ASSERT_TRUE(duplicate_result.is_err());
    EXPECT_TRUE(duplicate_result.unwrap_err().is_DuplicateSubcommand());

    auto alias = Command::make("tool");
    alias.add_subcommand(Command::make("run").alias("r"));
    alias.add_subcommand(Command::make("read").alias("r"));
    auto alias_result = rstd::move(alias).build();
    ASSERT_TRUE(alias_result.is_err());
    EXPECT_TRUE(alias_result.unwrap_err().is_DuplicateSubcommand());
}

TEST(ArgparseSubcommands, UsesActualArgv0AndAliasForDisplayOnly) {
    auto child = Command::make("run");
    child.alias("r");
    child.version("2.0");
    auto root = Command::make("tool");
    root.add_subcommand(rstd::move(child));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(subcommand_argv("renamed", "r", "--version"));
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_Display());
    EXPECT_EQ(result->as_Display().request.text(), "renamed r 2.0");
    EXPECT_EQ(parser.name(), "tool");
    EXPECT_EQ(parser.render_usage(), "Usage: tool [OPTIONS] [COMMAND]");
}
