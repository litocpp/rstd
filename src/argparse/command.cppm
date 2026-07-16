export module rstd.argparse:command;
export import :parser;

using namespace rstd::prelude;
using rstd::collections::BTreeMap;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::sync::Arc;

Atomic<u64> next_command_token { 1 };

auto make_option_name(char short_name) -> String {
    auto name = String::make("-");
    name.push_back(short_name);
    return name;
}

auto make_option_name(ref<str> long_name) -> String {
    auto name = String::make("--");
    name.push_str(long_name);
    return name;
}

auto valid_long_name(ref<str> name) noexcept -> bool {
    if (name.size() == 0) return false;
    for (usize i = 0; i < name.size(); ++i) {
        const u8   byte  = name.data()[i];
        const bool valid = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                           (byte >= '0' && byte <= '9') || byte == '-' || byte == '_';
        if (! valid) return false;
    }
    return true;
}

void prefix_command_path(Arc<CompiledCommand>& schema, ref<str> parent) {
    auto  borrowed       = schema.get_mut();
    auto& command        = *borrowed.unwrap();
    command.command_path = rstd::format("{} {}", parent, command.name.as_str());
    for (usize i = 0; i < command.subcommands.len(); ++i) {
        prefix_command_path(command.subcommands[i].schema, command.command_path.as_str());
    }
}

struct GroupMember {
    u64   command;
    usize slot;
};

struct ConflictDefinition {
    GroupMember left;
    GroupMember right;
};

struct RequirementDefinition {
    GroupMember source;
    bool        target_is_group;
    GroupMember target;
};

export namespace rstd::argparse
{

class GroupKey {
    u64   command_;
    usize slot_;

    GroupKey(u64 command, usize slot): command_(command), slot_(slot) {}
    friend class Command;

public:
    GroupKey(const GroupKey&)            = default;
    GroupKey& operator=(const GroupKey&) = default;
};

class ArgGroup {
    String           id_;
    Vec<GroupMember> members_;
    bool             required_ { false };
    bool             multiple_ { true };

    friend class Command;
    explicit ArgGroup(ref<str> id): id_(String::make(id)), members_(Vec<GroupMember>::make()) {}

public:
    ArgGroup(const ArgGroup&)            = delete;
    ArgGroup& operator=(const ArgGroup&) = delete;
    ArgGroup(ArgGroup&&)                 = default;
    ArgGroup& operator=(ArgGroup&&)      = default;

    static auto make(ref<str> id) -> ArgGroup { return ArgGroup { id }; }

    template<typename T>
    auto arg(const ArgKey<T>& key) & -> ArgGroup& {
        members_.push(GroupMember { key.command_, key.slot_ });
        return *this;
    }
    template<typename T>
    auto arg(const ArgKey<T>& key) && -> ArgGroup&& {
        arg(key);
        return rstd::move(*this);
    }
    auto required(bool value = true) & -> ArgGroup& {
        required_ = value;
        return *this;
    }
    auto required(bool value = true) && -> ArgGroup&& {
        required(value);
        return rstd::move(*this);
    }
    auto multiple(bool value = true) & -> ArgGroup& {
        multiple_ = value;
        return *this;
    }
    auto multiple(bool value = true) && -> ArgGroup&& {
        multiple(value);
        return rstd::move(*this);
    }
};

class Command {
    String                     name_;
    Option<String>             about_;
    Option<String>             long_about_;
    Option<String>             version_;
    Option<String>             after_help_;
    Vec<String>                aliases_;
    Vec<ArgSpec>               args_;
    Vec<ArgGroup>              groups_;
    Vec<ConflictDefinition>    conflicts_;
    Vec<RequirementDefinition> requirements_;
    Vec<Command>               subcommands_;
    u64                        command_token_;
    bool                       auto_help_ { true };
    bool                       auto_version_ { true };
    bool                       subcommand_required_ { false };

public:
    explicit Command(ref<str> name)
        : name_(String::make(name)),
          aliases_(Vec<String>::make()),
          args_(Vec<ArgSpec>::make()),
          groups_(Vec<ArgGroup>::make()),
          conflicts_(Vec<ConflictDefinition>::make()),
          requirements_(Vec<RequirementDefinition>::make()),
          subcommands_(Vec<Command>::make()),
          command_token_(next_command_token.fetch_add(1, Ordering::Relaxed)) {}

    Command(const Command&)            = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&)                 = default;
    Command& operator=(Command&&)      = default;

    static auto make(ref<str> name) -> Command { return Command { name }; }

    auto about(ref<str> text) & -> Command& {
        about_ = Some(String::make(text));
        return *this;
    }
    auto about(ref<str> text) && -> Command&& {
        about(text);
        return rstd::move(*this);
    }
    auto version(ref<str> text) & -> Command& {
        version_ = Some(String::make(text));
        return *this;
    }
    auto version(ref<str> text) && -> Command&& {
        version(text);
        return rstd::move(*this);
    }
    auto long_about(ref<str> text) & -> Command& {
        long_about_ = Some(String::make(text));
        return *this;
    }
    auto long_about(ref<str> text) && -> Command&& {
        long_about(text);
        return rstd::move(*this);
    }
    auto after_help(ref<str> text) & -> Command& {
        after_help_ = Some(String::make(text));
        return *this;
    }
    auto after_help(ref<str> text) && -> Command&& {
        after_help(text);
        return rstd::move(*this);
    }
    auto alias(ref<str> name) & -> Command& {
        aliases_.push(String::make(name));
        return *this;
    }
    auto alias(ref<str> name) && -> Command&& {
        alias(name);
        return rstd::move(*this);
    }
    auto disable_help(bool value = true) & -> Command& {
        auto_help_ = ! value;
        return *this;
    }
    auto disable_help(bool value = true) && -> Command&& {
        disable_help(value);
        return rstd::move(*this);
    }
    auto disable_version(bool value = true) & -> Command& {
        auto_version_ = ! value;
        return *this;
    }
    auto disable_version(bool value = true) && -> Command&& {
        disable_version(value);
        return rstd::move(*this);
    }
    auto require_subcommand(bool value = true) & -> Command& {
        subcommand_required_ = value;
        return *this;
    }
    auto require_subcommand(bool value = true) && -> Command&& {
        require_subcommand(value);
        return rstd::move(*this);
    }

    template<typename T>
    auto add_arg(Arg<T>&& argument) -> ArgKey<T> {
        const usize slot = args_.len();
        args_.push(::erase_arg<T>(rstd::move(argument)));
        return ArgKey<T> { command_token_, slot };
    }

    auto add_group(ArgGroup&& group) -> GroupKey {
        const usize slot = groups_.len();
        groups_.push(rstd::move(group));
        return GroupKey { command_token_, slot };
    }

    auto add_subcommand(Command&& command) & -> Command& {
        subcommands_.push(rstd::move(command));
        return *this;
    }
    auto add_subcommand(Command&& command) && -> Command&& {
        add_subcommand(rstd::move(command));
        return rstd::move(*this);
    }

    template<typename L, typename R>
    auto conflicts(const ArgKey<L>& left, const ArgKey<R>& right) & -> Command& {
        conflicts_.push(ConflictDefinition {
            GroupMember { left.command_, left.slot_ },
            GroupMember { right.command_, right.slot_ },
        });
        return *this;
    }
    template<typename L, typename R>
    auto conflicts(const ArgKey<L>& left, const ArgKey<R>& right) && -> Command&& {
        conflicts(left, right);
        return rstd::move(*this);
    }

    template<typename S, typename T>
    auto requires_arg(const ArgKey<S>& source, const ArgKey<T>& target) & -> Command& {
        requirements_.push(RequirementDefinition {
            GroupMember { source.command_, source.slot_ },
            false,
            GroupMember { target.command_, target.slot_ },
        });
        return *this;
    }
    template<typename S, typename T>
    auto requires_arg(const ArgKey<S>& source, const ArgKey<T>& target) && -> Command&& {
        requires_arg(source, target);
        return rstd::move(*this);
    }
    template<typename S>
    auto requires_arg(const ArgKey<S>& source, const GroupKey& target) & -> Command& {
        requirements_.push(RequirementDefinition {
            GroupMember { source.command_, source.slot_ },
            true,
            GroupMember { target.command_, target.slot_ },
        });
        return *this;
    }
    template<typename S>
    auto requires_arg(const ArgKey<S>& source, const GroupKey& target) && -> Command&& {
        requires_arg(source, target);
        return rstd::move(*this);
    }

    auto build() && -> Result<Parser, DefinitionError> {
        if (name_.is_empty()) {
            return Err(DefinitionError::InvalidCommandName(name_.clone()));
        }

        if (auto_help_) {
            auto help = Arg<bool>::help_action("help");
            help.short_name('h');
            help.long_name("help");
            help.help("Print help");
            add_arg(rstd::move(help));
        }
        if (auto_version_ && version_.is_some()) {
            auto version = Arg<bool>::version_action("version");
            version.short_name('V');
            version.long_name("version");
            version.help("Print version");
            add_arg(rstd::move(version));
        }

        auto id_index     = BTreeMap<String, usize>::make();
        auto option_index = BTreeMap<String, usize>::make();
        auto positionals  = Vec<usize>::make();

        for (usize slot = 0; slot < args_.len(); ++slot) {
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

            const bool takes_value = argument.action.is_Set() || argument.action.is_Append();
            if (takes_value && argument.num_args.maximum().is_some() &&
                *argument.num_args.maximum() == 0) {
                return Err(DefinitionError::IncompatibleAction(argument.id.clone()));
            }
            if (! takes_value &&
                (argument.num_args.minimum() != 0 || argument.num_args.maximum().is_none() ||
                 *argument.num_args.maximum() != 0)) {
                return Err(DefinitionError::IncompatibleAction(argument.id.clone()));
            }

            if (argument.default_raw_value.is_some()) {
                auto parsed =
                    argument.parser->parse_default(argument.default_raw_value->as_os_str());
                if (parsed.is_err()) {
                    return Err(DefinitionError::InvalidDefaultValue(
                        argument.id.clone(), rstd::move(parsed).unwrap_err()));
                }
                argument.default_value = Some(rstd::move(parsed).unwrap());
            }
            if (argument.implicit_raw_value.is_some()) {
                if (! takes_value || argument.num_args.minimum() != 0) {
                    return Err(DefinitionError::IncompatibleAction(argument.id.clone()));
                }
                auto parsed =
                    argument.parser->parse_default(argument.implicit_raw_value->as_os_str());
                if (parsed.is_err()) {
                    return Err(DefinitionError::InvalidImplicitValue(
                        argument.id.clone(), rstd::move(parsed).unwrap_err()));
                }
                argument.implicit_value = Some(rstd::move(parsed).unwrap());
            }

            bool has_option_name = false;
            if (argument.short_name.is_some()) {
                const char short_name = *argument.short_name;
                if (short_name < '!' || short_name > '~' || short_name == '-' ||
                    short_name == '=') {
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
            for (usize alias_index = 0; alias_index < argument.short_aliases.len(); ++alias_index) {
                auto& alias = argument.short_aliases[alias_index];
                if (! valid_long_name(alias.as_str())) {
                    return Err(DefinitionError::InvalidShortName('-'));
                }
                auto name = String::make("-");
                name.push_str(alias.as_str());
                if (option_index.insert(name.clone(), slot).is_some()) {
                    return Err(DefinitionError::DuplicateOption(rstd::move(name)));
                }
                has_option_name = true;
            }
            for (usize alias_index = 0; alias_index < argument.aliases.len(); ++alias_index) {
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
        for (usize i = 0; i < positionals.len(); ++i) positional_reserve.push(usize { 0 });
        usize reserve   = 0;
        usize unbounded = 0;
        for (usize i = positionals.len(); i > 0; --i) {
            const usize position         = i - 1;
            const auto& argument         = args_[positionals[position]];
            positional_reserve[position] = reserve;
            reserve += argument.num_args.minimum();
            if (argument.num_args.maximum().is_none()) {
                ++unbounded;
                if (unbounded > 1) {
                    return Err(DefinitionError::InvalidValueCount(argument.id.clone()));
                }
            }
        }

        auto group_index = BTreeMap<String, usize>::make();
        auto groups      = Vec<CompiledGroup>::with_capacity(groups_.len());
        for (usize group_slot = 0; group_slot < groups_.len(); ++group_slot) {
            auto& group = groups_[group_slot];
            if (group.id_.is_empty() || group.members_.is_empty() ||
                id_index.contains_key(group.id_.as_str())) {
                return Err(DefinitionError::InvalidGroup(group.id_.clone()));
            }
            if (group_index.insert(group.id_.clone(), group_slot).is_some()) {
                return Err(DefinitionError::DuplicateGroupId(group.id_.clone()));
            }
            auto members = Vec<usize>::with_capacity(group.members_.len());
            for (usize member_index = 0; member_index < group.members_.len(); ++member_index) {
                const auto& member = group.members_[member_index];
                if (member.command != command_token_ || member.slot >= args_.len()) {
                    return Err(DefinitionError::ForeignKey());
                }
                for (usize previous = 0; previous < member_index; ++previous) {
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
        for (usize relation = 0; relation < conflicts_.len(); ++relation) {
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
        for (usize relation = 0; relation < requirements_.len(); ++relation) {
            const auto& definition = requirements_[relation];
            const usize target_len = definition.target_is_group ? groups.len() : args_.len();
            if (definition.source.command != command_token_ ||
                definition.target.command != command_token_ ||
                definition.source.slot >= args_.len() || definition.target.slot >= target_len ||
                (! definition.target_is_group &&
                 definition.source.slot == definition.target.slot)) {
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
        for (usize subcommand_slot = 0; subcommand_slot < subcommands_.len(); ++subcommand_slot) {
            auto& command = subcommands_[subcommand_slot];
            auto  aliases = rstd::move(command.aliases_);
            auto  built   = rstd::move(command).build();
            if (built.is_err()) return Err(rstd::move(built).unwrap_err());
            auto parser = rstd::move(built).unwrap();
            prefix_command_path(parser.schema_, name_.as_str());
            auto name = parser.schema_->name.clone();
            if (subcommand_index.insert(name.clone(), subcommand_slot).is_some()) {
                return Err(DefinitionError::DuplicateSubcommand(rstd::move(name)));
            }
            for (usize alias_index = 0; alias_index < aliases.len(); ++alias_index) {
                if (! valid_long_name(aliases[alias_index].as_str()) ||
                    subcommand_index.insert(aliases[alias_index].clone(), subcommand_slot)
                        .is_some()) {
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
};

} // namespace rstd::argparse
