#include <rstd/test/gtest.hpp>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using namespace rstd::literals;
using rstd::ffi::OsString;

template<typename... Tokens>
auto subcommand_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(tokens)), ...);
    return values;
}

TEST(ArgparseSubcommands, OwnsSchemaAndReturnsRecursiveMatches) {
    auto serve = Command::make("serve"_str);
    serve.about("Run the server"_str);
    serve.alias("s"_str);
    auto serve_command = serve.key();
    auto port          = serve.add_arg(
        Arg<String>::value("port"_str, string_parser()).long_name("port"_str).required());

    auto root = Command::make("tool"_str);
    root.require_subcommand();
    root.add_subcommand(rstd::move(serve));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(subcommand_argv("tool"_str, "s"_str, "--port"_str, "8080"_str));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto matches = rstd::move(outcome).as_Parsed().value;
    auto child   = matches.subcommand_matches("serve"_str);
    ASSERT_TRUE(child.is_some());
    auto typed_child = matches.subcommand_matches(serve_command);
    ASSERT_TRUE(typed_child.is_some());
    auto value = (*child)->get_one(port);
    ASSERT_TRUE(value.is_ok());
    ASSERT_TRUE(value->is_some());
    EXPECT_EQ(***value, "8080"_str);
    ASSERT_TRUE(matches.subcommand().is_some());
    EXPECT_EQ(matches.subcommand()->get<0>(), "serve"_str);
}

TEST(ArgparseSubcommands, TypedKeysMatchOnlyTheSelectedDirectChild) {
    auto leaf     = Command::make("leaf"_str);
    auto leaf_key = leaf.key();

    auto branch     = Command::make("branch"_str);
    auto branch_key = branch.key();
    branch.add_subcommand(rstd::move(leaf));

    auto sibling     = Command::make("sibling"_str);
    auto sibling_key = sibling.key();

    auto foreign     = Command::make("foreign"_str);
    auto foreign_key = foreign.key();

    auto root = Command::make("tool"_str);
    root.require_subcommand();
    root.add_subcommand(rstd::move(branch));
    root.add_subcommand(rstd::move(sibling));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(subcommand_argv("tool"_str, "branch"_str, "leaf"_str));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto matches = rstd::move(outcome).as_Parsed().value;

    auto selected = matches.subcommand_matches(branch_key);
    ASSERT_TRUE(selected.is_some());
    EXPECT_TRUE(matches.subcommand_matches(sibling_key).is_none());
    EXPECT_TRUE(matches.subcommand_matches(foreign_key).is_none());
    EXPECT_TRUE(matches.subcommand_matches(leaf_key).is_none());
    EXPECT_TRUE((*selected)->subcommand_matches(leaf_key).is_some());
}

TEST(ArgparseSubcommands, ValidatesRequiredAndInvalidSubcommands) {
    auto root = Command::make("tool"_str);
    root.require_subcommand();
    root.add_subcommand(Command::make("run"_str));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto missing = parser.parse_from(subcommand_argv("tool"_str));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().is_MissingSubcommand());

    auto invalid = parser.parse_from(subcommand_argv("tool"_str, "other"_str));
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().is_InvalidSubcommand());

    auto help = parser.parse_from(subcommand_argv("tool"_str, "run"_str, "--help"_str));
    ASSERT_TRUE(help.is_ok());
    ASSERT_TRUE(help->is_Display());
    EXPECT_EQ(help->as_Display().request.kind(), DisplayKind::Tag::Help);
}

TEST(ArgparseSubcommands, EndOfOptionsForcesSameNameIntoPositional) {
    auto root  = Command::make("tool"_str);
    auto value = root.add_arg(Arg<String>::value("value"_str, string_parser()));
    root.add_subcommand(Command::make("run"_str));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(subcommand_argv("tool"_str, "--"_str, "run"_str));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto matches = rstd::move(outcome).as_Parsed().value;
    EXPECT_TRUE(matches.subcommand().is_none());
    auto parsed = matches.get_one(value);
    ASSERT_TRUE(parsed.is_ok());
    EXPECT_EQ(***parsed, "run"_str);
}

TEST(ArgparseSubcommands, KnownParsingUnifiesParentAndChildUnknownTokens) {
    auto root = Command::make("tool"_str);
    root.add_subcommand(Command::make("run"_str));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_known_from(
        subcommand_argv("tool"_str, "--parent-unknown"_str, "run"_str, "--child-unknown"_str));
    ASSERT_TRUE(result.is_ok());
    auto outcome = rstd::move(result).unwrap();
    auto known   = rstd::move(outcome).as_Parsed().value;
    ASSERT_EQ(known.unknown().len(), usize(2));
    EXPECT_EQ(known.unknown()[usize()].as_os_str().to_str(), Some("--parent-unknown"_str));
    EXPECT_EQ(known.unknown()[usize(1)].as_os_str().to_str(), Some("--child-unknown"_str));
}

TEST(ArgparseSubcommands, RejectsDuplicateNamesAndAliases) {
    auto duplicate = Command::make("tool"_str);
    duplicate.add_subcommand(Command::make("run"_str));
    duplicate.add_subcommand(Command::make("run"_str));
    auto duplicate_result = rstd::move(duplicate).build();
    ASSERT_TRUE(duplicate_result.is_err());
    EXPECT_TRUE(duplicate_result.unwrap_err().is_DuplicateSubcommand());

    auto alias = Command::make("tool"_str);
    alias.add_subcommand(Command::make("run"_str).alias("r"_str));
    alias.add_subcommand(Command::make("read"_str).alias("r"_str));
    auto alias_result = rstd::move(alias).build();
    ASSERT_TRUE(alias_result.is_err());
    EXPECT_TRUE(alias_result.unwrap_err().is_DuplicateSubcommand());
}

TEST(ArgparseSubcommands, UsesActualArgv0AndAliasForDisplayOnly) {
    auto child = Command::make("run"_str);
    child.alias("r"_str);
    child.version("2.0"_str);
    auto root = Command::make("tool"_str);
    root.add_subcommand(rstd::move(child));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto result = parser.parse_from(subcommand_argv("renamed"_str, "r"_str, "--version"_str));
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_Display());
    EXPECT_EQ(result->as_Display().request.text(), "renamed r 2.0"_str);
    EXPECT_EQ(parser.name(), "tool"_str);
    EXPECT_EQ(parser.render_usage(), "Usage: tool [OPTIONS] [COMMAND]"_str);
}
