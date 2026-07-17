#include <gtest/gtest.h>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using rstd::ffi::OsString;

template<typename... Tokens>
auto relation_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(ref<str>(tokens))), ...);
    return values;
}

TEST(ArgparseRelations, ValidatesRequiredAndExclusiveGroups) {
    auto command = Command::make("tool");
    auto file    = command.add_arg(Arg<bool>::flag("file").long_name("file"));
    auto stream  = command.add_arg(Arg<bool>::flag("stdin").long_name("stdin"));
    command.add_group(ArgGroup::make("input").arg(file).arg(stream).required().multiple(false));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto missing = parser.parse_from(relation_argv("tool"));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().is_MissingRequiredGroup());

    auto conflict = parser.parse_from(relation_argv("tool", "--file", "--stdin"));
    ASSERT_TRUE(conflict.is_err());
    EXPECT_TRUE(conflict.unwrap_err().is_ArgumentConflict());

    auto valid = parser.parse_from(relation_argv("tool", "--file"));
    EXPECT_TRUE(valid.is_ok());
}

TEST(ArgparseRelations, ValidatesConflictsAndRequirementsAfterRecognition) {
    auto command = Command::make("tool");
    auto config  = command.add_arg(Arg<bool>::flag("config").long_name("config"));
    auto force   = command.add_arg(Arg<bool>::flag("force").long_name("force"));
    auto dry_run = command.add_arg(Arg<bool>::flag("dry-run").long_name("dry-run"));
    auto file    = command.add_arg(Arg<bool>::flag("file").long_name("file"));
    auto stream  = command.add_arg(Arg<bool>::flag("stdin").long_name("stdin"));
    auto input   = command.add_group(ArgGroup::make("input").arg(file).arg(stream));
    command.conflicts(force, dry_run);
    command.requires_arg(config, input);

    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto conflict = parser.parse_from(relation_argv("tool", "--force", "--dry-run"));
    ASSERT_TRUE(conflict.is_err());
    EXPECT_TRUE(conflict.unwrap_err().is_ArgumentConflict());

    auto missing = parser.parse_from(relation_argv("tool", "--config"));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().is_MissingRequiredGroup());

    auto valid = parser.parse_from(relation_argv("tool", "--config", "--stdin"));
    EXPECT_TRUE(valid.is_ok());
}

TEST(ArgparseRelations, DefaultsDoNotParticipateInRelations) {
    auto command = Command::make("tool");
    auto left    = command.add_arg(
        Arg<String>::value("left", string_parser()).long_name("left").default_value("left"));
    auto right = command.add_arg(
        Arg<String>::value("right", string_parser()).long_name("right").default_value("right"));
    command.add_group(ArgGroup::make("selection").arg(left).arg(right).multiple(false));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    EXPECT_TRUE(parser.parse_from(relation_argv("tool")).is_ok());
    EXPECT_TRUE(parser.parse_from(relation_argv("tool", "--left", "selected")).is_ok());

    auto conflict =
        parser.parse_from(relation_argv("tool", "--left", "selected", "--right", "selected"));
    ASSERT_TRUE(conflict.is_err());
    EXPECT_TRUE(conflict.unwrap_err().is_ArgumentConflict());
}

TEST(ArgparseRelations, RejectsForeignKeysAndSelfRelationsDuringBuild) {
    auto first      = Command::make("first");
    auto first_key  = first.add_arg(Arg<bool>::flag("first").long_name("first"));
    auto second     = Command::make("second");
    auto second_key = second.add_arg(Arg<bool>::flag("second").long_name("second"));
    first.conflicts(first_key, second_key);
    auto foreign = rstd::move(first).build();
    ASSERT_TRUE(foreign.is_err());
    EXPECT_TRUE(foreign.unwrap_err().is_ForeignKey());

    auto self = Command::make("self");
    auto key  = self.add_arg(Arg<bool>::flag("value").long_name("value"));
    self.conflicts(key, key);
    auto invalid = rstd::move(self).build();
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().is_InvalidRelation());
}
