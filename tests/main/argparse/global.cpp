#include <rstd/test/gtest.hpp>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using namespace rstd::literals;
using rstd::ffi::OsString;

namespace
{

template<typename... Tokens>
auto global_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(tokens)), ...);
    return values;
}

struct GlobalSchema {
    ArgKey<String> name;
    ArgKey<u8>     verbose;
    CommandKey     outer;
    CommandKey     inner;
    Parser         parser;
};

auto global_schema() -> GlobalSchema {
    auto inner     = Command::make("inner"_str);
    auto inner_key = inner.key();

    auto outer     = Command::make("outer"_str);
    auto outer_key = outer.key();
    outer.add_subcommand(rstd::move(inner));

    auto root    = Command::make("tool"_str);
    auto name    = root.add_arg(Arg<String>::value("name"_str, string_parser())
                                    .long_name("name"_str)
                                    .default_value("default"_str)
                                    .global());
    auto verbose = root.add_arg(Arg<u8>::count("verbose"_str).short_name(u8('v')).global());
    root.add_subcommand(rstd::move(outer));
    auto built = rstd::move(root).build();
    return GlobalSchema {
        .name    = name,
        .verbose = verbose,
        .outer   = outer_key,
        .inner   = inner_key,
        .parser  = rstd::move(built).unwrap(),
    };
}

} // namespace

TEST(ArgparseGlobal, ParsesAtEveryCommandDepthWithTypedKeys) {
    auto schema = global_schema();
    auto result = schema.parser.parse_from(global_argv("tool"_str,
                                                       "--name"_str,
                                                       "root"_str,
                                                       "-v"_str,
                                                       "outer"_str,
                                                       "inner"_str,
                                                       "--name"_str,
                                                       "leaf"_str,
                                                       "-vv"_str));
    ASSERT_TRUE(result.is_ok());
    auto matches = rstd::move(result).unwrap().as_Parsed().value;
    auto outer   = matches.subcommand_matches(schema.outer);
    ASSERT_TRUE(outer.is_some());
    auto inner = (*outer)->subcommand_matches(schema.inner);
    ASSERT_TRUE(inner.is_some());

    auto verify = [&](ref<Matches> current) {
        auto name = current->get_one(schema.name);
        ASSERT_TRUE(name.is_ok());
        ASSERT_TRUE(name->is_some());
        EXPECT_EQ(***name, "leaf"_str);
        auto verbose = current->get_one(schema.verbose);
        ASSERT_TRUE(verbose.is_ok());
        ASSERT_TRUE(verbose->is_some());
        EXPECT_EQ(***verbose, u8(2));
    };
    verify(ref<Matches>::from_raw_parts(rstd::addressof(matches)));
    verify(*outer);
    verify(*inner);
    EXPECT_EQ(matches.occurrences("verbose"_str), usize(2));
    ASSERT_TRUE(matches.indices("verbose"_str).is_some());
    EXPECT_EQ(matches.indices("verbose"_str)->len(), usize(2));
}

TEST(ArgparseGlobal, PropagatesDefaultsAndRendersDescendantHelp) {
    auto schema = global_schema();
    auto result = schema.parser.parse_from(global_argv("tool"_str, "outer"_str, "inner"_str));
    ASSERT_TRUE(result.is_ok());
    auto matches = rstd::move(result).unwrap().as_Parsed().value;
    auto inner   = (*matches.subcommand_matches(schema.outer))->subcommand_matches(schema.inner);
    ASSERT_TRUE(inner.is_some());
    auto name = (*inner)->get_one(schema.name);
    ASSERT_TRUE(name.is_ok());
    ASSERT_TRUE(name->is_some());
    EXPECT_EQ(***name, "default"_str);
    auto source = (*inner)->value_source("name"_str);
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(source->is_DefaultValue());

    auto help =
        schema.parser.parse_from(global_argv("tool"_str, "outer"_str, "inner"_str, "--help"_str));
    ASSERT_TRUE(help.is_ok());
    ASSERT_TRUE(help->is_Display());
    EXPECT_TRUE(help->as_Display().request.text().contains("--name"_str));
    EXPECT_TRUE(help->as_Display().request.text().contains("-v"_str));
}

TEST(ArgparseGlobal, RejectsRequiredArguments) {
    auto root = Command::make("tool"_str);
    root.add_arg(
        Arg<String>::value("name"_str, string_parser()).long_name("name"_str).required().global());
    root.add_subcommand(Command::make("run"_str));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_err());
    EXPECT_TRUE(built.unwrap_err().is_IncompatibleAction());
}

TEST(ArgparseGlobal, PropagatesOnlyFromTheDeclaringCommandDownward) {
    auto leaf       = Command::make("leaf"_str);
    auto leaf_key   = leaf.key();
    auto branch     = Command::make("branch"_str);
    auto branch_key = branch.key();
    auto scope      = branch.add_arg(
        Arg<String>::value("scope"_str, string_parser()).long_name("scope"_str).global());
    branch.add_subcommand(rstd::move(leaf));
    auto root = Command::make("tool"_str);
    root.add_subcommand(rstd::move(branch));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto result = built->parse_from(
        global_argv("tool"_str, "branch"_str, "leaf"_str, "--scope"_str, "nested"_str));
    ASSERT_TRUE(result.is_ok());
    auto matches = rstd::move(result).unwrap().as_Parsed().value;
    EXPECT_TRUE(matches.get_one(scope).is_err());
    auto branch_matches = matches.subcommand_matches(branch_key);
    ASSERT_TRUE(branch_matches.is_some());
    auto leaf_matches = (*branch_matches)->subcommand_matches(leaf_key);
    ASSERT_TRUE(leaf_matches.is_some());
    ASSERT_TRUE((*branch_matches)->get_one(scope).is_ok());
    auto value = (*leaf_matches)->get_one(scope);
    ASSERT_TRUE(value.is_ok());
    ASSERT_TRUE(value->is_some());
    EXPECT_EQ(***value, "nested"_str);
}

TEST(ArgparseGlobal, UsesDescendantValuesForDeclaringCommandRelations) {
    auto root   = Command::make("tool"_str);
    auto global = root.add_arg(Arg<bool>::flag("global"_str).long_name("global"_str).global());
    auto local  = root.add_arg(Arg<bool>::flag("local"_str).long_name("local"_str));
    root.conflicts(global, local);
    root.add_subcommand(Command::make("run"_str));
    auto built = rstd::move(root).build();
    ASSERT_TRUE(built.is_ok());
    auto result =
        built->parse_from(global_argv("tool"_str, "--local"_str, "run"_str, "--global"_str));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(result.unwrap_err().is_ArgumentConflict());
}
