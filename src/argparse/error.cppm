module;
#include <rstd/enum.hpp>

export module rstd.argparse:error;
export import rstd;

using namespace rstd::prelude;
using rstd::ffi::OsString;

export namespace rstd::argparse
{

#define RSTD_ARGPARSE_VALUE_ERROR_VARIANTS(V) \
    V(InvalidUtf8, ())                        \
    V(Message, (String message;))

class ValueError {
    RSTD_ENUM_BODY(ValueError, RSTD_ARGPARSE_VALUE_ERROR_VARIANTS)
    [[nodiscard]]
    constexpr auto kind() const noexcept -> Tag {
        return tag();
    }
};

#define RSTD_ARGPARSE_DEFINITION_ERROR_VARIANTS(V)          \
    V(InvalidCommandName, (String name;))                   \
    V(InvalidArgumentId, (String id;))                      \
    V(InvalidShortName, (char name;))                       \
    V(InvalidLongName, (String name;))                      \
    V(DuplicateArgumentId, (String id;))                    \
    V(DuplicateOption, (String name;))                      \
    V(InvalidValueCount, (String id;))                      \
    V(IncompatibleAction, (String id;))                     \
    V(InvalidDefaultValue, (String id; ValueError error;))  \
    V(InvalidImplicitValue, (String id; ValueError error;)) \
    V(InvalidGroup, (String id;))                           \
    V(DuplicateGroupId, (String id;))                       \
    V(ForeignKey, ())                                       \
    V(InvalidRelation, ())                                  \
    V(DuplicateSubcommand, (String name;))

class DefinitionError {
    RSTD_ENUM_BODY(DefinitionError, RSTD_ARGPARSE_DEFINITION_ERROR_VARIANTS)
    [[nodiscard]]
    constexpr auto kind() const noexcept -> Tag {
        return tag();
    }
};

class Parser;

class ParseContext {
    String command_path_;
    String usage_;

    friend class ParseError;

public:
    static auto make() -> ParseContext { return {}; }
};

#define RSTD_ARGPARSE_PARSE_ERROR_VARIANTS(V)                                                     \
    V(UnknownArgument,                                                                            \
      (ParseContext context; OsString token; usize index; Option<String> suggestion;))            \
    V(MissingValue, (ParseContext context; String id; usize index;))                              \
    V(TooFewValues, (ParseContext context; String id; usize minimum; usize actual; usize index;)) \
    V(TooManyValues, (ParseContext context; String id; usize maximum; usize index;))              \
    V(InvalidValue,                                                                               \
      (ParseContext context; String id; OsString value; usize index; ValueError error;))          \
    V(InvalidUtf8Value, (ParseContext context; String id; OsString value; usize index;))          \
    V(DuplicateArgument, (ParseContext context; String id; usize index;))                         \
    V(ArgumentConflict, (ParseContext context; String left; String right;))                       \
    V(MissingRequiredArgument, (ParseContext context; String id;))                                \
    V(MissingRequiredGroup, (ParseContext context; String id;))                                   \
    V(MissingSubcommand, (ParseContext context; String command;))                                 \
    V(InvalidSubcommand, (ParseContext context; OsString name; usize index;))                     \
    V(UnexpectedPositional, (ParseContext context; OsString token; usize index;))

class ParseError {
    RSTD_ENUM_BODY(ParseError, RSTD_ARGPARSE_PARSE_ERROR_VARIANTS)

    static auto UnknownArgument(OsString token, usize index, Option<String> suggestion) -> Self {
        return Self::UnknownArgument(
            ParseContext::make(), rstd::move(token), index, rstd::move(suggestion));
    }
    static auto MissingValue(String id, usize index) -> Self {
        return Self::MissingValue(ParseContext::make(), rstd::move(id), index);
    }
    static auto TooFewValues(String id, usize minimum, usize actual, usize index) -> Self {
        return Self::TooFewValues(ParseContext::make(), rstd::move(id), minimum, actual, index);
    }
    static auto TooManyValues(String id, usize maximum, usize index) -> Self {
        return Self::TooManyValues(ParseContext::make(), rstd::move(id), maximum, index);
    }
    static auto InvalidValue(String id, OsString value, usize index, ValueError error) -> Self {
        return Self::InvalidValue(
            ParseContext::make(), rstd::move(id), rstd::move(value), index, rstd::move(error));
    }
    static auto InvalidUtf8Value(String id, OsString value, usize index) -> Self {
        return Self::InvalidUtf8Value(
            ParseContext::make(), rstd::move(id), rstd::move(value), index);
    }
    static auto DuplicateArgument(String id, usize index) -> Self {
        return Self::DuplicateArgument(ParseContext::make(), rstd::move(id), index);
    }
    static auto ArgumentConflict(String left, String right) -> Self {
        return Self::ArgumentConflict(ParseContext::make(), rstd::move(left), rstd::move(right));
    }
    static auto MissingRequiredArgument(String id) -> Self {
        return Self::MissingRequiredArgument(ParseContext::make(), rstd::move(id));
    }
    static auto MissingRequiredGroup(String id) -> Self {
        return Self::MissingRequiredGroup(ParseContext::make(), rstd::move(id));
    }
    static auto MissingSubcommand(String command) -> Self {
        return Self::MissingSubcommand(ParseContext::make(), rstd::move(command));
    }
    static auto InvalidSubcommand(OsString name, usize index) -> Self {
        return Self::InvalidSubcommand(ParseContext::make(), rstd::move(name), index);
    }
    static auto UnexpectedPositional(OsString token, usize index) -> Self {
        return Self::UnexpectedPositional(ParseContext::make(), rstd::move(token), index);
    }

    [[nodiscard]]
    constexpr auto kind() const noexcept -> Tag {
        return tag();
    }

private:
    auto context_mut() -> ParseContext& {
        switch (tag()) {
        case Tag::UnknownArgument: return as_UnknownArgument().context;
        case Tag::MissingValue: return as_MissingValue().context;
        case Tag::TooFewValues: return as_TooFewValues().context;
        case Tag::TooManyValues: return as_TooManyValues().context;
        case Tag::InvalidValue: return as_InvalidValue().context;
        case Tag::InvalidUtf8Value: return as_InvalidUtf8Value().context;
        case Tag::DuplicateArgument: return as_DuplicateArgument().context;
        case Tag::ArgumentConflict: return as_ArgumentConflict().context;
        case Tag::MissingRequiredArgument: return as_MissingRequiredArgument().context;
        case Tag::MissingRequiredGroup: return as_MissingRequiredGroup().context;
        case Tag::MissingSubcommand: return as_MissingSubcommand().context;
        case Tag::InvalidSubcommand: return as_InvalidSubcommand().context;
        case Tag::UnexpectedPositional: return as_UnexpectedPositional().context;
        }
        rstd::unreachable();
    }
    auto context() const -> const ParseContext& {
        return const_cast<ParseError*>(this)->context_mut();
    }
    void attach_context(String command_path, String usage) {
        auto& value         = context_mut();
        value.command_path_ = rstd::move(command_path);
        value.usage_        = rstd::move(usage);
    }

    friend class Parser;

public:
    [[nodiscard]]
    auto command_path() const noexcept -> ref<str> {
        return context().command_path_.as_str();
    }
    [[nodiscard]]
    auto usage() const noexcept -> ref<str> {
        return context().usage_.as_str();
    }
};

#define RSTD_ARGPARSE_MATCH_ACCESS_ERROR_VARIANTS(V) \
    V(ForeignKey, ())                                \
    V(WrongType, ())                                 \
    V(IncompatibleAccessor, ())

class MatchAccessError {
    RSTD_ENUM_BODY(MatchAccessError, RSTD_ARGPARSE_MATCH_ACCESS_ERROR_VARIANTS)
    [[nodiscard]]
    constexpr auto kind() const noexcept -> Tag {
        return tag();
    }
};

#undef RSTD_ARGPARSE_VALUE_ERROR_VARIANTS
#undef RSTD_ARGPARSE_DEFINITION_ERROR_VARIANTS
#undef RSTD_ARGPARSE_PARSE_ERROR_VARIANTS
#undef RSTD_ARGPARSE_MATCH_ACCESS_ERROR_VARIANTS

} // namespace rstd::argparse

namespace rstd
{

template<>
struct Impl<fmt::Display, argparse::ValueError> : ImplBase<argparse::ValueError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        switch (error.tag()) {
        case argparse::ValueError::Tag::InvalidUtf8:
            return formatter.write_raw(
                reinterpret_cast<const u8*>("argument value is not valid UTF-8"),
                rstd::strlen("argument value is not valid UTF-8"));
        case argparse::ValueError::Tag::Message:
            return formatter.write_fmt(
                fmt::Arguments::make("{}", error.as_Message().message.as_str()));
        }
        return false;
    }
};

template<>
struct Impl<fmt::Debug, argparse::ValueError> : ImplBase<argparse::ValueError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<fmt::Display, argparse::DefinitionError> : ImplBase<argparse::DefinitionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        switch (error.tag()) {
        case argparse::DefinitionError::Tag::InvalidCommandName:
            return formatter.write_fmt(fmt::Arguments::make(
                "invalid command name '{}'", error.as_InvalidCommandName().name.as_str()));
        case argparse::DefinitionError::Tag::InvalidArgumentId:
            return formatter.write_fmt(fmt::Arguments::make(
                "invalid argument id '{}'", error.as_InvalidArgumentId().id.as_str()));
        case argparse::DefinitionError::Tag::InvalidShortName:
            return formatter.write_fmt(fmt::Arguments::make("invalid short option '-{}'",
                                                            error.as_InvalidShortName().name));
        case argparse::DefinitionError::Tag::InvalidLongName:
            return formatter.write_fmt(fmt::Arguments::make(
                "invalid long option '--{}'", error.as_InvalidLongName().name.as_str()));
        case argparse::DefinitionError::Tag::DuplicateArgumentId:
            return formatter.write_fmt(fmt::Arguments::make(
                "duplicate argument id '{}'", error.as_DuplicateArgumentId().id.as_str()));
        case argparse::DefinitionError::Tag::DuplicateOption:
            return formatter.write_fmt(fmt::Arguments::make(
                "duplicate option '{}'", error.as_DuplicateOption().name.as_str()));
        case argparse::DefinitionError::Tag::InvalidValueCount:
            return formatter.write_fmt(fmt::Arguments::make(
                "invalid value count for argument '{}'", error.as_InvalidValueCount().id.as_str()));
        case argparse::DefinitionError::Tag::IncompatibleAction:
            return formatter.write_fmt(
                fmt::Arguments::make("incompatible action for argument '{}'",
                                     error.as_IncompatibleAction().id.as_str()));
        case argparse::DefinitionError::Tag::InvalidDefaultValue:
            return formatter.write_fmt(
                fmt::Arguments::make("invalid default value for argument '{}': {}",
                                     error.as_InvalidDefaultValue().id.as_str(),
                                     error.as_InvalidDefaultValue().error));
        case argparse::DefinitionError::Tag::InvalidImplicitValue:
            return formatter.write_fmt(
                fmt::Arguments::make("invalid implicit value for argument '{}': {}",
                                     error.as_InvalidImplicitValue().id.as_str(),
                                     error.as_InvalidImplicitValue().error));
        case argparse::DefinitionError::Tag::InvalidGroup:
            return formatter.write_fmt(fmt::Arguments::make("invalid argument group '{}'",
                                                            error.as_InvalidGroup().id.as_str()));
        case argparse::DefinitionError::Tag::DuplicateGroupId:
            return formatter.write_fmt(fmt::Arguments::make(
                "duplicate argument group '{}'", error.as_DuplicateGroupId().id.as_str()));
        case argparse::DefinitionError::Tag::ForeignKey:
            return formatter.write_raw(
                reinterpret_cast<const u8*>("schema key belongs to a different command"),
                rstd::strlen("schema key belongs to a different command"));
        case argparse::DefinitionError::Tag::InvalidRelation:
            return formatter.write_raw(reinterpret_cast<const u8*>("invalid argument relation"),
                                       rstd::strlen("invalid argument relation"));
        case argparse::DefinitionError::Tag::DuplicateSubcommand:
            return formatter.write_fmt(fmt::Arguments::make(
                "duplicate subcommand '{}'", error.as_DuplicateSubcommand().name.as_str()));
        }
        return false;
    }
};

template<>
struct Impl<fmt::Debug, argparse::DefinitionError> : ImplBase<argparse::DefinitionError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<fmt::Display, argparse::ParseError> : ImplBase<argparse::ParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        switch (error.tag()) {
        case argparse::ParseError::Tag::UnknownArgument:
            if (error.as_UnknownArgument().suggestion.is_some()) {
                return formatter.write_fmt(
                    fmt::Arguments::make("unknown argument '{}'; did you mean '{}'?",
                                         error.as_UnknownArgument().token.as_os_str(),
                                         error.as_UnknownArgument().suggestion->as_str()));
            }
            return formatter.write_fmt(fmt::Arguments::make(
                "unknown argument '{}'", error.as_UnknownArgument().token.as_os_str()));
        case argparse::ParseError::Tag::MissingValue:
            return formatter.write_fmt(fmt::Arguments::make("missing value for argument '{}'",
                                                            error.as_MissingValue().id.as_str()));
        case argparse::ParseError::Tag::TooFewValues:
            return formatter.write_fmt(fmt::Arguments::make("too few values for argument '{}'",
                                                            error.as_TooFewValues().id.as_str()));
        case argparse::ParseError::Tag::TooManyValues:
            return formatter.write_fmt(fmt::Arguments::make("too many values for argument '{}'",
                                                            error.as_TooManyValues().id.as_str()));
        case argparse::ParseError::Tag::InvalidValue:
            return formatter.write_fmt(
                fmt::Arguments::make("invalid value '{}' for argument '{}': {}",
                                     error.as_InvalidValue().value.as_os_str(),
                                     error.as_InvalidValue().id.as_str(),
                                     error.as_InvalidValue().error));
        case argparse::ParseError::Tag::InvalidUtf8Value:
            return formatter.write_fmt(
                fmt::Arguments::make("value '{}' for argument '{}' is not valid UTF-8",
                                     error.as_InvalidUtf8Value().value.as_os_str(),
                                     error.as_InvalidUtf8Value().id.as_str()));
        case argparse::ParseError::Tag::DuplicateArgument:
            return formatter.write_fmt(
                fmt::Arguments::make("argument '{}' cannot be used multiple times",
                                     error.as_DuplicateArgument().id.as_str()));
        case argparse::ParseError::Tag::ArgumentConflict:
            return formatter.write_fmt(
                fmt::Arguments::make("argument '{}' conflicts with '{}'",
                                     error.as_ArgumentConflict().left.as_str(),
                                     error.as_ArgumentConflict().right.as_str()));
        case argparse::ParseError::Tag::MissingRequiredArgument:
            return formatter.write_fmt(
                fmt::Arguments::make("required argument '{}' was not provided",
                                     error.as_MissingRequiredArgument().id.as_str()));
        case argparse::ParseError::Tag::MissingRequiredGroup:
            return formatter.write_fmt(
                fmt::Arguments::make("required argument group '{}' was not provided",
                                     error.as_MissingRequiredGroup().id.as_str()));
        case argparse::ParseError::Tag::MissingSubcommand:
            return formatter.write_fmt(
                fmt::Arguments::make("command '{}' requires a subcommand",
                                     error.as_MissingSubcommand().command.as_str()));
        case argparse::ParseError::Tag::InvalidSubcommand:
            return formatter.write_fmt(fmt::Arguments::make(
                "invalid subcommand '{}'", error.as_InvalidSubcommand().name.as_os_str()));
        case argparse::ParseError::Tag::UnexpectedPositional:
            return formatter.write_fmt(
                fmt::Arguments::make("unexpected positional argument '{}'",
                                     error.as_UnexpectedPositional().token.as_os_str()));
        }
        return false;
    }
};

template<>
struct Impl<fmt::Debug, argparse::ParseError> : ImplBase<argparse::ParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<fmt::Display, argparse::MatchAccessError> : ImplBase<argparse::MatchAccessError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        switch (error.tag()) {
        case argparse::MatchAccessError::Tag::ForeignKey:
            return formatter.write_raw(
                reinterpret_cast<const u8*>("argument key belongs to a different command"),
                rstd::strlen("argument key belongs to a different command"));
        case argparse::MatchAccessError::Tag::WrongType:
            return formatter.write_raw(
                reinterpret_cast<const u8*>("argument key has the wrong value type"),
                rstd::strlen("argument key has the wrong value type"));
        case argparse::MatchAccessError::Tag::IncompatibleAccessor:
            return formatter.write_raw(
                reinterpret_cast<const u8*>("accessor is incompatible with the argument action"),
                rstd::strlen("accessor is incompatible with the argument action"));
        }
        return false;
    }
};

template<>
struct Impl<fmt::Debug, argparse::MatchAccessError> : ImplBase<argparse::MatchAccessError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

} // namespace rstd
