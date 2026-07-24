#include <gtest/gtest.h>

import rstd.argparse;

using namespace rstd::prelude;
using namespace rstd::argparse;
using namespace rstd::literals;
using rstd::ffi::OsString;

template<typename... Tokens>
auto relation_argv(Tokens... tokens) -> Vec<OsString> {
    auto values = Vec<OsString>::make();
    (values.push(OsString::from(tokens)), ...);
    return values;
}

TEST(ArgparseRelations, ValidatesRequiredAndExclusiveGroups) {
    auto command = Command::make("tool"_str);
    auto file    = command.add_arg(Arg<bool>::flag("file"_str).long_name("file"_str));
    auto stream  = command.add_arg(Arg<bool>::flag("stdin"_str).long_name("stdin"_str));
    command.add_group(ArgGroup::make("input"_str).arg(file).arg(stream).required().multiple(false));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto missing = parser.parse_from(relation_argv("tool"_str));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().is_MissingRequiredGroup());

    auto conflict = parser.parse_from(relation_argv("tool"_str, "--file"_str, "--stdin"_str));
    ASSERT_TRUE(conflict.is_err());
    EXPECT_TRUE(conflict.unwrap_err().is_ArgumentConflict());

    auto valid = parser.parse_from(relation_argv("tool"_str, "--file"_str));
    EXPECT_TRUE(valid.is_ok());
}

TEST(ArgparseRelations, ValidatesConflictsAndRequirementsAfterRecognition) {
    auto command = Command::make("tool"_str);
    auto config  = command.add_arg(Arg<bool>::flag("config"_str).long_name("config"_str));
    auto force   = command.add_arg(Arg<bool>::flag("force"_str).long_name("force"_str));
    auto dry_run = command.add_arg(Arg<bool>::flag("dry-run"_str).long_name("dry-run"_str));
    auto file    = command.add_arg(Arg<bool>::flag("file"_str).long_name("file"_str));
    auto stream  = command.add_arg(Arg<bool>::flag("stdin"_str).long_name("stdin"_str));
    auto input   = command.add_group(ArgGroup::make("input"_str).arg(file).arg(stream));
    command.conflicts(force, dry_run);
    command.requires_arg(config, input);

    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    auto conflict = parser.parse_from(relation_argv("tool"_str, "--force"_str, "--dry-run"_str));
    ASSERT_TRUE(conflict.is_err());
    EXPECT_TRUE(conflict.unwrap_err().is_ArgumentConflict());

    auto missing = parser.parse_from(relation_argv("tool"_str, "--config"_str));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().is_MissingRequiredGroup());

    auto valid = parser.parse_from(relation_argv("tool"_str, "--config"_str, "--stdin"_str));
    EXPECT_TRUE(valid.is_ok());
}

TEST(ArgparseRelations, DefaultsDoNotParticipateInRelations) {
    auto command = Command::make("tool"_str);
    auto left    = command.add_arg(Arg<String>::value("left"_str, string_parser())
                                       .long_name("left"_str)
                                       .default_value("left"_str));
    auto right   = command.add_arg(Arg<String>::value("right"_str, string_parser())
                                       .long_name("right"_str)
                                       .default_value("right"_str));
    command.add_group(ArgGroup::make("selection"_str).arg(left).arg(right).multiple(false));
    auto built = rstd::move(command).build();
    ASSERT_TRUE(built.is_ok());
    auto parser = rstd::move(built).unwrap();

    EXPECT_TRUE(parser.parse_from(relation_argv("tool"_str)).is_ok());
    EXPECT_TRUE(parser.parse_from(relation_argv("tool"_str, "--left"_str, "selected"_str)).is_ok());

    auto conflict = parser.parse_from(
        relation_argv("tool"_str, "--left"_str, "selected"_str, "--right"_str, "selected"_str));
    ASSERT_TRUE(conflict.is_err());
    EXPECT_TRUE(conflict.unwrap_err().is_ArgumentConflict());
}

TEST(ArgparseRelations, RejectsForeignKeysAndSelfRelationsDuringBuild) {
    auto first      = Command::make("first"_str);
    auto first_key  = first.add_arg(Arg<bool>::flag("first"_str).long_name("first"_str));
    auto second     = Command::make("second"_str);
    auto second_key = second.add_arg(Arg<bool>::flag("second"_str).long_name("second"_str));
    first.conflicts(first_key, second_key);
    auto foreign = rstd::move(first).build();
    ASSERT_TRUE(foreign.is_err());
    EXPECT_TRUE(foreign.unwrap_err().is_ForeignKey());

    auto self = Command::make("self"_str);
    auto key  = self.add_arg(Arg<bool>::flag("value"_str).long_name("value"_str));
    self.conflicts(key, key);
    auto invalid = rstd::move(self).build();
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().is_InvalidRelation());
}
