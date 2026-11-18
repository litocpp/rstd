export module rstd.argparse:command;
export import :command_key;
export import :parser;

using namespace rstd::prelude;
using namespace rstd::literals;

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
    explicit Command(ref<str> name);

    Command(const Command&)            = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&)                 = default;
    Command& operator=(Command&&)      = default;

    static auto make(ref<str> name) -> Command { return Command { name }; }

    [[nodiscard]]
    auto key() const noexcept -> CommandKey {
        return CommandKey { command_token_ };
    }

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

private:
    auto build_with_globals(Vec<ArgSpec> inherited_globals) -> Result<Parser, DefinitionError>;

public:
    auto build() && -> Result<Parser, DefinitionError>;
};

} // namespace rstd::argparse
