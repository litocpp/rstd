module rstd.argparse;
import :command;
import :parser;
import :schema;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::collections::BTreeMap;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::sync::Arc;

Atomic<u64> next_command_token { u64(1) };

auto make_option_name(u8 short_name) -> String {
    auto name = String::make("-"_str);
    name.push_ascii(short_name);
    return name;
}

auto make_option_name(ref<str> long_name) -> String {
    auto name = String::make("--"_str);
    name.push_str(long_name);
    return name;
}

auto valid_long_name(ref<str> name) noexcept -> bool {
    if (name.size() == usize()) return false;
    for (usize i {}; i < name.size(); ++i) {
        const u8   byte  = name[i];
        const bool valid = (byte >= u8('a') && byte <= u8('z')) ||
                           (byte >= u8('A') && byte <= u8('Z')) ||
                           (byte >= u8('0') && byte <= u8('9')) || byte == u8('-') ||
                           byte == u8('_') || byte == u8('.');
        if (! valid) return false;
    }
    return true;
}

auto clone_action(const rstd::argparse::ArgAction& action) -> rstd::argparse::ArgAction {
    using Action = rstd::argparse::ArgAction;
    switch (action.tag()) {
    case Action::Tag::Set: return Action::Set();
    case Action::Tag::Append: return Action::Append();
    case Action::Tag::SetTrue: return Action::SetTrue();
    case Action::Tag::SetFalse: return Action::SetFalse();
    case Action::Tag::Count: return Action::Count();
    case Action::Tag::Help: return Action::Help();
    case Action::Tag::Version: return Action::Version();
    }
    rstd::unreachable();
}

auto clone_arg_spec(const ArgSpec& argument) -> ArgSpec {
    return ArgSpec {
        .id                  = argument.id.clone(),
        .short_name          = argument.short_name,
        .long_name           = argument.long_name.clone(),
        .short_aliases       = argument.short_aliases.clone(),
        .aliases             = argument.aliases.clone(),
        .help                = argument.help.clone(),
        .value_name          = argument.value_name.clone(),
        .help_heading        = argument.help_heading.clone(),
        .possible_values     = argument.possible_values.clone(),
        .num_args            = argument.num_args,
        .action              = clone_action(argument.action),
        .required            = argument.required,
        .hidden              = argument.hidden,
        .allow_hyphen_values = argument.allow_hyphen_values,
        .global              = argument.global,
        .type_id             = argument.type_id,
        .parser              = argument.parser.clone(),
        .default_raw_value   = argument.default_raw_value.clone(),
        .default_value       = argument.default_value.clone(),
        .implicit_raw_value  = argument.implicit_raw_value.clone(),
        .implicit_value      = argument.implicit_value.clone(),
        .owner_command       = argument.owner_command,
        .owner_slot          = argument.owner_slot,
    };
}

auto clone_globals(const Vec<ArgSpec>& arguments) -> Vec<ArgSpec> {
    auto result = Vec<ArgSpec>::with_capacity(arguments.len());
    for (const auto& argument : arguments) result.push(clone_arg_spec(argument));
    return result;
}

void prefix_command_path(Arc<CompiledCommand>& schema, ref<str> parent) {
    auto  borrowed       = schema.get_mut();
    auto& command        = *borrowed.unwrap();
    command.command_path = rstd::format("{} {}", parent, command.name);
    for (usize i {}; i < command.subcommands.len(); ++i) {
        prefix_command_path(command.subcommands[i].schema, command.command_path.as_str());
    }
}

namespace rstd::argparse
{
Command::Command(ref<str> name)
    : name_(String::make(name)),
      aliases_(Vec<String>::make()),
      args_(Vec<ArgSpec>::make()),
      groups_(Vec<ArgGroup>::make()),
      conflicts_(Vec<ConflictDefinition>::make()),
      requirements_(Vec<RequirementDefinition>::make()),
      subcommands_(Vec<Command>::make()),
      command_token_(next_command_token.fetch_add(u64(1), Ordering::Relaxed)) {
}

auto Command::build_with_globals(Vec<ArgSpec> inherited_globals)
    -> Result<Parser, DefinitionError> {
    if (name_.is_empty()) {
        return Err(DefinitionError::InvalidCommandName(name_.clone()));
    }

    const usize local_args = args_.len();
    for (usize slot {}; slot < local_args; ++slot) {
        args_[slot].owner_command = command_token_;
        args_[slot].owner_slot    = slot;
    }
    for (auto& inherited : inherited_globals) {
        bool shadowed = false;
        for (usize slot {}; slot < local_args; ++slot) {
            if (args_[slot].id.as_str() == inherited.id.as_str()) {
                shadowed = true;
                break;
            }
        }
        if (! shadowed) args_.push(rstd::move(inherited));
    }

    if (auto_help_) {
        auto help = Arg<bool>::help_action("help"_str);
        help.short_name(u8('h'));
        help.long_name("help"_str);
        help.help("Print help"_str);
        add_arg(rstd::move(help));
    }
    if (auto_version_ && version_.is_some()) {
        auto version = Arg<bool>::version_action("version"_str);
        version.short_name(u8('V'));
        version.long_name("version"_str);
        version.help("Print version"_str);
        add_arg(rstd::move(version));
    }

    auto id_index     = BTreeMap<String, usize>::make();
    auto option_index = BTreeMap<String, usize>::make();
    auto positionals  = Vec<usize>::make();

    for (usize slot {}; slot < args_.len(); ++slot) {
        auto& argument = args_[slot];
        if (argument.id.is_empty()) {
            return Err(DefinitionError::InvalidArgumentId(argument.id.clone()));
        }
        if (id_index.insert(argument.id.clone(), slot).is_some()) {
            return Err(DefinitionError::DuplicateArgumentId(argument.id.clone()));
        }
        if (! argument.num_args.valid()) {
            return Err(DefinitionError::InvalidValueCount(argument.id.clone()));
        }
        if (argument.global && argument.required) {
            return Err(DefinitionError::IncompatibleAction(argument.id.clone()));
        }

        const bool takes_value = argument.action.is_Set() || argument.action.is_Append();
        if (takes_value && argument.num_args.maximum().is_some() &&
            *argument.num_args.maximum() == usize()) {
            return Err(DefinitionError::IncompatibleAction(argument.id.clone()));
        }
        if (! takes_value &&
            (argument.num_args.minimum() != usize() || argument.num_args.maximum().is_none() ||
             *argument.num_args.maximum() != usize())) {
            return Err(DefinitionError::IncompatibleAction(argument.id.clone()));
        }

        if (argument.default_raw_value.is_some() && argument.default_value.is_none()) {
            auto parsed = argument.parser->parse_default(argument.default_raw_value->as_os_str());
            if (parsed.is_err()) {
                return Err(DefinitionError::InvalidDefaultValue(argument.id.clone(),
                                                                rstd::move(parsed).unwrap_err()));
            }
            argument.default_value = Some(rstd::move(parsed).unwrap());
        }
        if (argument.implicit_raw_value.is_some() && argument.implicit_value.is_none()) {
            if (! takes_value || argument.num_args.minimum() != usize()) {
                return Err(DefinitionError::IncompatibleAction(argument.id.clone()));
            }
            auto parsed = argument.parser->parse_default(argument.implicit_raw_value->as_os_str());
            if (parsed.is_err()) {
                return Err(DefinitionError::InvalidImplicitValue(argument.id.clone(),
                                                                 rstd::move(parsed).unwrap_err()));
            }
            argument.implicit_value = Some(rstd::move(parsed).unwrap());
        }

        bool has_option_name = false;
        if (argument.short_name.is_some()) {
            const u8 short_name = *argument.short_name;
            if (short_name < u8('!') || short_name > u8('~') || short_name == u8('-') ||
                short_name == u8('=')) {
                return Err(DefinitionError::InvalidShortName(short_name));
            }
            auto name = make_option_name(short_name);
            if (option_index.insert(name.clone(), slot).is_some()) {
                return Err(DefinitionError::DuplicateOption(rstd::move(name)));
            }
            has_option_name = true;
        }
        if (argument.long_name.is_some()) {
            auto& long_name = *argument.long_name;
            if (! valid_long_name(long_name.as_str())) {
                return Err(DefinitionError::InvalidLongName(long_name.clone()));
            }
            auto name = make_option_name(long_name.as_str());
            if (option_index.insert(name.clone(), slot).is_some()) {
                return Err(DefinitionError::DuplicateOption(rstd::move(name)));
            }
            has_option_name = true;
        }
        for (usize alias_index {}; alias_index < argument.short_aliases.len(); ++alias_index) {
            auto& alias = argument.short_aliases[alias_index];
            if (! valid_long_name(alias.as_str())) {
                return Err(DefinitionError::InvalidShortName(u8('-')));
            }
            auto name = String::make("-"_str);
            name.push_str(alias.as_str());
            if (option_index.insert(name.clone(), slot).is_some()) {
                return Err(DefinitionError::DuplicateOption(rstd::move(name)));
            }
            has_option_name = true;
        }
        for (usize alias_index {}; alias_index < argument.aliases.len(); ++alias_index) {
            auto& alias = argument.aliases[alias_index];
            if (! valid_long_name(alias.as_str())) {
                return Err(DefinitionError::InvalidLongName(alias.clone()));
            }
            auto name = make_option_name(alias.as_str());
            if (option_index.insert(name.clone(), slot).is_some()) {
                return Err(DefinitionError::DuplicateOption(rstd::move(name)));
            }
            has_option_name = true;
        }
        if (! has_option_name) {
            if (! takes_value) {
                return Err(DefinitionError::IncompatibleAction(argument.id.clone()));
            }
            positionals.push(usize(slot));
        }
    }

    auto positional_reserve = Vec<usize>::with_capacity(positionals.len());
    for (usize i {}; i < positionals.len(); ++i) {
        positional_reserve.push(usize());
    }
    usize reserve {};
    usize unbounded {};
    for (usize i = positionals.len(); i > usize(); --i) {
        const usize position         = i - usize(1);
        const auto& argument         = args_[positionals[position]];
        positional_reserve[position] = reserve;
        reserve += argument.num_args.minimum();
        if (argument.num_args.maximum().is_none()) {
            ++unbounded;
            if (unbounded > usize(1)) {
                return Err(DefinitionError::InvalidValueCount(argument.id.clone()));
            }
        }
    }

    auto group_index = BTreeMap<String, usize>::make();
    auto groups      = Vec<CompiledGroup>::with_capacity(groups_.len());
    for (usize group_slot {}; group_slot < groups_.len(); ++group_slot) {
        auto& group = groups_[group_slot];
        if (group.id_.is_empty() || group.members_.is_empty() ||
            id_index.contains_key(group.id_.as_str())) {
            return Err(DefinitionError::InvalidGroup(group.id_.clone()));
        }
        if (group_index.insert(group.id_.clone(), group_slot).is_some()) {
            return Err(DefinitionError::DuplicateGroupId(group.id_.clone()));
        }
        auto members = Vec<usize>::with_capacity(group.members_.len());
        for (usize member_index {}; member_index < group.members_.len(); ++member_index) {
            const auto& member = group.members_[member_index];
            if (member.command != command_token_ || member.slot >= args_.len()) {
                return Err(DefinitionError::ForeignKey());
            }
            for (usize previous {}; previous < member_index; ++previous) {
                if (group.members_[previous].slot == member.slot) {
                    return Err(DefinitionError::InvalidGroup(group.id_.clone()));
                }
            }
            members.push(usize(member.slot));
        }
        groups.push(CompiledGroup {
            rstd::move(group.id_), rstd::move(members), group.required_, group.multiple_ });
    }

    auto conflicts = Vec<CompiledConflict>::with_capacity(conflicts_.len());
    for (usize relation {}; relation < conflicts_.len(); ++relation) {
        const auto& definition = conflicts_[relation];
        if (definition.left.command != command_token_ ||
            definition.right.command != command_token_ || definition.left.slot >= args_.len() ||
            definition.right.slot >= args_.len()) {
            return Err(DefinitionError::ForeignKey());
        }
        if (definition.left.slot == definition.right.slot) {
            return Err(DefinitionError::InvalidRelation());
        }
        conflicts.push(CompiledConflict { definition.left.slot, definition.right.slot });
    }

    auto requirements = Vec<CompiledRequirement>::with_capacity(requirements_.len());
    for (usize relation {}; relation < requirements_.len(); ++relation) {
        const auto& definition = requirements_[relation];
        const usize target_len = definition.target_is_group ? groups.len() : args_.len();
        if (definition.source.command != command_token_ ||
            definition.target.command != command_token_ || definition.source.slot >= args_.len() ||
            definition.target.slot >= target_len ||
            (! definition.target_is_group && definition.source.slot == definition.target.slot)) {
            return Err(definition.source.command == command_token_ &&
                               definition.target.command == command_token_
                           ? DefinitionError::InvalidRelation()
                           : DefinitionError::ForeignKey());
        }
        requirements.push(CompiledRequirement {
            definition.source.slot, definition.target_is_group, definition.target.slot });
    }

    auto subcommands      = Vec<CompiledSubcommand>::with_capacity(subcommands_.len());
    auto subcommand_index = BTreeMap<String, usize>::make();
    auto globals          = Vec<ArgSpec>::make();
    for (const auto& argument : args_) {
        if (argument.global) globals.push(clone_arg_spec(argument));
    }
    for (usize subcommand_slot {}; subcommand_slot < subcommands_.len(); ++subcommand_slot) {
        auto& command = subcommands_[subcommand_slot];
        auto  aliases = rstd::move(command.aliases_);
        auto  built   = command.build_with_globals(clone_globals(globals));
        if (built.is_err()) return Err(rstd::move(built).unwrap_err());
        auto parser = rstd::move(built).unwrap();
        prefix_command_path(parser.schema_, name_.as_str());
        auto name = parser.schema_->name.clone();
        if (subcommand_index.insert(name.clone(), subcommand_slot).is_some()) {
            return Err(DefinitionError::DuplicateSubcommand(rstd::move(name)));
        }
        for (usize alias_index {}; alias_index < aliases.len(); ++alias_index) {
            if (! valid_long_name(aliases[alias_index].as_str()) ||
                subcommand_index.insert(aliases[alias_index].clone(), subcommand_slot).is_some()) {
                return Err(DefinitionError::DuplicateSubcommand(aliases[alias_index].clone()));
            }
        }
        subcommands.push(CompiledSubcommand {
            rstd::move(name), rstd::move(aliases), rstd::move(parser.schema_) });
    }

    auto schema = Arc<CompiledCommand>::make(rstd::move(name_),
                                             rstd::move(about_),
                                             rstd::move(long_about_),
                                             rstd::move(version_),
                                             rstd::move(after_help_),
                                             rstd::move(args_),
                                             rstd::move(id_index),
                                             rstd::move(option_index),
                                             rstd::move(positionals),
                                             rstd::move(positional_reserve),
                                             rstd::move(groups),
                                             rstd::move(conflicts),
                                             rstd::move(requirements),
                                             rstd::move(subcommands),
                                             rstd::move(subcommand_index),
                                             subcommand_required_,
                                             command_token_);
    return Ok(Parser { rstd::move(schema) });
}

auto Command::build() && -> Result<Parser, DefinitionError> {
    return build_with_globals(Vec<ArgSpec>::make());
}
} // namespace rstd::argparse
