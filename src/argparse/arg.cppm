module;
#include <rstd/enum.hpp>

export module rstd.argparse:arg;
export import :value_parser;

using namespace rstd::prelude;
using rstd::any::TypeId;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

export namespace rstd::argparse
{

#define RSTD_ARGPARSE_ACTION_VARIANTS(V) \
    V(Set)                               \
    V(Append)                            \
    V(SetTrue)                           \
    V(SetFalse)                          \
    V(Count)                             \
    V(Help)                              \
    V(Version)

RSTD_TAG_ENUM_WITH_DEFAULT(ArgAction, RSTD_ARGPARSE_ACTION_VARIANTS, Set)

#undef RSTD_ARGPARSE_ACTION_VARIANTS

class NumArgs {
    usize minimum_;
    usize maximum_;
    bool  unbounded_;

    constexpr NumArgs(usize minimum, usize maximum, bool unbounded) noexcept
        : minimum_(minimum), maximum_(maximum), unbounded_(unbounded) {}

public:
    static constexpr auto exact(usize count) noexcept -> NumArgs {
        return NumArgs { count, count, false };
    }
    static constexpr auto optional() noexcept -> NumArgs { return NumArgs { 0, 1, false }; }
    static constexpr auto any() noexcept -> NumArgs { return NumArgs { 0, 0, true }; }
    static constexpr auto at_least_one() noexcept -> NumArgs { return NumArgs { 1, 0, true }; }
    static constexpr auto range(usize minimum, usize maximum) noexcept -> NumArgs {
        return NumArgs { minimum, maximum, false };
    }

    [[nodiscard]]
    constexpr auto minimum() const noexcept -> usize {
        return minimum_;
    }
    [[nodiscard]]
    constexpr auto maximum() const noexcept -> Option<usize> {
        return unbounded_ ? None() : Some(usize(maximum_));
    }
    [[nodiscard]]
    constexpr auto contains(usize count) const noexcept -> bool {
        return count >= minimum_ && (unbounded_ || count <= maximum_);
    }
    [[nodiscard]]
    constexpr auto valid() const noexcept -> bool {
        return unbounded_ || minimum_ <= maximum_;
    }
};

template<typename T>
class ArgKey {
    u64   command_;
    usize slot_;

    constexpr ArgKey(u64 command, usize slot) noexcept: command_(command), slot_(slot) {}

    friend class Command;
    friend class Matches;
    friend class ArgGroup;

public:
    constexpr ArgKey(const ArgKey&)            = default;
    constexpr ArgKey& operator=(const ArgKey&) = default;
};

template<typename T>
class Arg;

} // namespace rstd::argparse

struct ArgSpec {
    String                               id;
    Option<char>                         short_name;
    Option<String>                       long_name;
    Vec<String>                          short_aliases;
    Vec<String>                          aliases;
    String                               help;
    String                               value_name;
    String                               help_heading;
    Vec<String>                          possible_values;
    rstd::argparse::NumArgs              num_args;
    rstd::argparse::ArgAction            action;
    bool                                 required;
    bool                                 hidden;
    bool                                 allow_hyphen_values;
    TypeId                               type_id;
    Box<dyn<ErasedValueParser>>          parser;
    Option<OsString>                     default_raw_value;
    Option<Box<dyn<ErasedDefaultValue>>> default_value;
    Option<OsString>                     implicit_raw_value;
    Option<Box<dyn<ErasedDefaultValue>>> implicit_value;
};

template<typename T>
auto erase_arg(rstd::argparse::Arg<T>&& argument) -> ArgSpec;

template<typename T>
class NoValueParser {
public:
    auto parse(ref<OsStr>) const -> Result<T, rstd::argparse::ValueError> {
        return Err(rstd::argparse::ValueError::Message(
            String::make("argument action does not accept a value")));
    }
};

template<typename P>
auto value_parser_metadata(const P& parser) -> Vec<String> {
    if constexpr (requires { parser.possible_values(); }) {
        return parser.possible_values();
    } else {
        return Vec<String>::make();
    }
}

export namespace rstd::argparse
{

template<typename T>
class Arg {
    String                   id_;
    Option<char>             short_name_;
    Option<String>           long_name_;
    Vec<String>              short_aliases_;
    Vec<String>              aliases_;
    String                   help_;
    String                   value_name_;
    String                   help_heading_;
    Vec<String>              possible_values_;
    NumArgs                  num_args_;
    ArgAction                action_;
    bool                     required_ { false };
    bool                     hidden_ { false };
    bool                     allow_hyphen_values_ { false };
    Box<dyn<ValueParser<T>>> parser_;
    Option<OsString>         default_raw_value_;
    Option<OsString>         implicit_raw_value_;

    Arg(ref<str>                 id,
        Box<dyn<ValueParser<T>>> parser,
        ArgAction                action,
        NumArgs                  num_args,
        Vec<String>              possible_values = Vec<String>::make())
        : id_(String::make(id)),
          short_aliases_(Vec<String>::make()),
          aliases_(Vec<String>::make()),
          help_(String::make()),
          value_name_(String::make()),
          help_heading_(String::make()),
          possible_values_(rstd::move(possible_values)),
          num_args_(num_args),
          action_(rstd::move(action)),
          parser_(rstd::move(parser)) {}

    friend auto ::erase_arg<T>(Arg<T>&&) -> ::ArgSpec;

public:
    Arg(const Arg&)            = delete;
    Arg& operator=(const Arg&) = delete;
    Arg(Arg&&)                 = default;
    Arg& operator=(Arg&&)      = default;

    template<typename P>
        requires Impled<P, ValueParser<T>>
    static auto value(ref<str> id, P parser) -> Arg {
        auto possible_values = value_parser_metadata(parser);
        return Arg { id,
                     Box<dyn<ValueParser<T>>>::make(rstd::move(parser)),
                     ArgAction::Set(),
                     NumArgs::exact(1),
                     rstd::move(possible_values) };
    }

    static auto flag(ref<str> id) -> Arg
        requires mtp::same_as<T, bool>
    {
        return Arg { id,
                     Box<dyn<ValueParser<T>>>::make(NoValueParser<T> {}),
                     ArgAction::SetTrue(),
                     NumArgs::exact(0) };
    }

    static auto count(ref<str> id) -> Arg
        requires mtp::same_as<T, u8>
    {
        return Arg { id,
                     Box<dyn<ValueParser<T>>>::make(NoValueParser<T> {}),
                     ArgAction::Count(),
                     NumArgs::exact(0) };
    }

    static auto help_action(ref<str> id) -> Arg
        requires mtp::same_as<T, bool>
    {
        return Arg { id,
                     Box<dyn<ValueParser<T>>>::make(NoValueParser<T> {}),
                     ArgAction::Help(),
                     NumArgs::exact(0) };
    }

    static auto version_action(ref<str> id) -> Arg
        requires mtp::same_as<T, bool>
    {
        return Arg { id,
                     Box<dyn<ValueParser<T>>>::make(NoValueParser<T> {}),
                     ArgAction::Version(),
                     NumArgs::exact(0) };
    }

    auto short_name(char name) & -> Arg& {
        short_name_ = Some(char(name));
        return *this;
    }
    auto short_name(char name) && -> Arg&& {
        short_name(name);
        return rstd::move(*this);
    }
    auto long_name(ref<str> name) & -> Arg& {
        long_name_ = Some(String::make(name));
        return *this;
    }
    auto long_name(ref<str> name) && -> Arg&& {
        long_name(name);
        return rstd::move(*this);
    }
    auto alias(ref<str> name) & -> Arg& {
        aliases_.push(String::make(name));
        return *this;
    }
    auto alias(ref<str> name) && -> Arg&& {
        alias(name);
        return rstd::move(*this);
    }
    auto short_alias(ref<str> name) & -> Arg& {
        short_aliases_.push(String::make(name));
        return *this;
    }
    auto short_alias(ref<str> name) && -> Arg&& {
        short_alias(name);
        return rstd::move(*this);
    }
    auto help(ref<str> text) & -> Arg& {
        help_ = String::make(text);
        return *this;
    }
    auto help(ref<str> text) && -> Arg&& {
        help(text);
        return rstd::move(*this);
    }
    auto value_name(ref<str> name) & -> Arg& {
        value_name_ = String::make(name);
        return *this;
    }
    auto value_name(ref<str> name) && -> Arg&& {
        value_name(name);
        return rstd::move(*this);
    }
    auto help_heading(ref<str> heading) & -> Arg& {
        help_heading_ = String::make(heading);
        return *this;
    }
    auto help_heading(ref<str> heading) && -> Arg&& {
        help_heading(heading);
        return rstd::move(*this);
    }
    auto num_args(NumArgs count) & -> Arg& {
        num_args_ = count;
        return *this;
    }
    auto num_args(NumArgs count) && -> Arg&& {
        num_args(count);
        return rstd::move(*this);
    }
    auto required(bool value = true) & -> Arg& {
        required_ = value;
        return *this;
    }
    auto required(bool value = true) && -> Arg&& {
        required(value);
        return rstd::move(*this);
    }
    auto hidden(bool value = true) & -> Arg& {
        hidden_ = value;
        return *this;
    }
    auto hidden(bool value = true) && -> Arg&& {
        hidden(value);
        return rstd::move(*this);
    }
    auto allow_hyphen_values(bool value = true) & -> Arg& {
        allow_hyphen_values_ = value;
        return *this;
    }
    auto allow_hyphen_values(bool value = true) && -> Arg&& {
        allow_hyphen_values(value);
        return rstd::move(*this);
    }
    auto append() & -> Arg& {
        action_ = ArgAction::Append();
        return *this;
    }
    auto append() && -> Arg&& {
        append();
        return rstd::move(*this);
    }

    auto default_value(ref<OsStr> value) & -> Arg&
        requires Impled<T, rstd::clone::Clone>
    {
        default_raw_value_ = Some(OsString::from(value));
        return *this;
    }
    auto default_value(ref<OsStr> value) && -> Arg&&
        requires Impled<T, rstd::clone::Clone>
    {
        default_value(value);
        return rstd::move(*this);
    }
    auto default_value(OsString&& value) & -> Arg&
        requires Impled<T, rstd::clone::Clone>
    {
        default_raw_value_ = Some(rstd::move(value));
        return *this;
    }
    auto default_value(OsString&& value) && -> Arg&&
        requires Impled<T, rstd::clone::Clone>
    {
        default_value(rstd::move(value));
        return rstd::move(*this);
    }

    auto implicit_value(ref<OsStr> value) & -> Arg&
        requires Impled<T, rstd::clone::Clone>
    {
        implicit_raw_value_ = Some(OsString::from(value));
        return *this;
    }
    auto implicit_value(ref<OsStr> value) && -> Arg&&
        requires Impled<T, rstd::clone::Clone>
    {
        implicit_value(value);
        return rstd::move(*this);
    }
    auto implicit_value(OsString&& value) & -> Arg&
        requires Impled<T, rstd::clone::Clone>
    {
        implicit_raw_value_ = Some(rstd::move(value));
        return *this;
    }
    auto implicit_value(OsString&& value) && -> Arg&&
        requires Impled<T, rstd::clone::Clone>
    {
        implicit_value(rstd::move(value));
        return rstd::move(*this);
    }

    static auto set_false(ref<str> id) -> Arg
        requires mtp::same_as<T, bool>
    {
        return Arg { id,
                     Box<dyn<ValueParser<T>>>::make(NoValueParser<T> {}),
                     ArgAction::SetFalse(),
                     NumArgs::exact(0) };
    }
};

} // namespace rstd::argparse

template<typename T>
auto erase_arg(rstd::argparse::Arg<T>&& argument) -> ArgSpec {
    auto parser = Box<dyn<ErasedValueParser>>::make(
        ErasedValueParserAdapter<T> { rstd::move(argument.parser_) });
    return ArgSpec {
        .id                  = rstd::move(argument.id_),
        .short_name          = rstd::move(argument.short_name_),
        .long_name           = rstd::move(argument.long_name_),
        .short_aliases       = rstd::move(argument.short_aliases_),
        .aliases             = rstd::move(argument.aliases_),
        .help                = rstd::move(argument.help_),
        .value_name          = rstd::move(argument.value_name_),
        .help_heading        = rstd::move(argument.help_heading_),
        .possible_values     = rstd::move(argument.possible_values_),
        .num_args            = argument.num_args_,
        .action              = rstd::move(argument.action_),
        .required            = argument.required_,
        .hidden              = argument.hidden_,
        .allow_hyphen_values = argument.allow_hyphen_values_,
        .type_id             = TypeId::of<T>(),
        .parser              = rstd::move(parser),
        .default_raw_value   = rstd::move(argument.default_raw_value_),
        .default_value       = None(),
        .implicit_raw_value  = rstd::move(argument.implicit_raw_value_),
        .implicit_value      = None(),
    };
}
