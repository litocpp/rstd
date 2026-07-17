module;
#include <rstd/enum.hpp>

export module rstd.argparse:parser;
export import :matches;

using namespace rstd::prelude;
using rstd::collections::BTreeMap;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;
using rstd::sync::Arc;

auto clone_os(ref<OsStr> value) -> OsString {
    return OsString::from(value);
}

auto option_lookup_name(ref<OsStr> token) -> Option<ref<str>> {
    auto name = token;
    if (auto split = token.split_once('='); split.is_some()) name = split->template get<0>();
    auto text = name.to_str();
    if (text.is_none()) return None();
    return text;
}

auto lookup_option(const CompiledCommand& schema, ref<OsStr> token) -> Option<usize> {
    auto name = option_lookup_name(token);
    if (name.is_none()) return None();
    auto slot = schema.option_index.get(*name);
    return slot.is_some() ? Some(usize(**slot)) : None();
}

auto edit_distance(ref<str> left, ref<str> right) -> usize {
    auto previous = Vec<usize>::with_capacity(right.size() + 1);
    auto current  = Vec<usize>::with_capacity(right.size() + 1);
    for (usize column = 0; column <= right.size(); ++column) {
        previous.push(usize(column));
        current.push(usize { 0 });
    }
    for (usize row = 1; row <= left.size(); ++row) {
        current[0] = row;
        for (usize column = 1; column <= right.size(); ++column) {
            const usize substitution =
                previous[column - 1] + (left.data()[row - 1] == right.data()[column - 1] ? 0 : 1);
            const usize insertion = current[column - 1] + 1;
            const usize deletion  = previous[column] + 1;
            usize       best      = substitution < insertion ? substitution : insertion;
            if (deletion < best) best = deletion;
            current[column] = best;
        }
        auto swap = rstd::move(previous);
        previous  = rstd::move(current);
        current   = rstd::move(swap);
    }
    return previous[right.size()];
}

auto suggest_option(const CompiledCommand& schema, ref<OsStr> token) -> Option<String> {
    auto input = option_lookup_name(token);
    if (input.is_none()) return None();

    Option<String> suggestion    = None();
    usize          best_distance = rstd::numeric_limits<usize>::max();
    auto           consider      = [&](String candidate) {
        const usize distance = edit_distance(*input, candidate.as_str());
        if (distance < best_distance) {
            best_distance = distance;
            suggestion    = Some(rstd::move(candidate));
        }
    };

    for (usize slot = 0; slot < schema.args.len(); ++slot) {
        const auto& argument = schema.args[slot];
        if (argument.short_name.is_some()) {
            auto candidate = String::make("-");
            candidate.push_back(*argument.short_name);
            consider(rstd::move(candidate));
        }
        if (argument.long_name.is_some()) {
            auto candidate = String::make("--");
            candidate.push_str(argument.long_name->as_str());
            consider(rstd::move(candidate));
        }
        for (usize alias = 0; alias < argument.short_aliases.len(); ++alias) {
            auto candidate = String::make("-");
            candidate.push_str(argument.short_aliases[alias].as_str());
            consider(rstd::move(candidate));
        }
        for (usize alias = 0; alias < argument.aliases.len(); ++alias) {
            auto candidate = String::make("--");
            candidate.push_str(argument.aliases[alias].as_str());
            consider(rstd::move(candidate));
        }
    }
    return best_distance <= 3 ? rstd::move(suggestion) : None();
}

auto is_option_like(ref<OsStr> token) -> bool {
    return token.len() > 1 && token.data()[0] == '-';
}

auto count_positional_candidates(const CompiledCommand& schema,
                                 const Vec<OsString>&   argv,
                                 usize                  start,
                                 bool                   options) -> usize {
    usize count = 0;
    for (usize index = start; index < argv.len(); ++index) {
        auto token = argv[index].as_os_str();
        if (options && token.len() == 2 && token.data()[0] == '-' && token.data()[1] == '-') {
            options = false;
            continue;
        }
        if (options && is_option_like(token)) {
            auto exact = lookup_option(schema, token);
            if (exact.is_some()) {
                const auto& spec = schema.args[*exact];
                if ((spec.action.is_Set() || spec.action.is_Append()) &&
                    token.split_once('=').is_none()) {
                    usize skip = spec.num_args.minimum();
                    while (skip != 0 && index + 1 < argv.len()) {
                        ++index;
                        --skip;
                    }
                }
                continue;
            }
            if (token.data()[0] == '-' && token.len() > 1) {
                for (usize offset = 1; offset < token.len(); ++offset) {
                    char name[3] = { '-', static_cast<char>(token.data()[offset]), '\0' };
                    auto slot    = schema.option_index.get(ref<str>(name));
                    if (slot.is_none()) break;
                    const auto& spec = schema.args[**slot];
                    if (spec.action.is_Set() || spec.action.is_Append()) {
                        if (offset + 1 == token.len()) {
                            usize skip = spec.num_args.minimum();
                            while (skip != 0 && index + 1 < argv.len()) {
                                ++index;
                                --skip;
                            }
                        }
                        break;
                    }
                }
            }
            continue;
        }
        if (options) {
            auto text = token.to_str();
            if (text.is_some() && schema.subcommand_index.contains_key(*text)) break;
        }
        ++count;
    }
    return count;
}

struct IndexedValue {
    OsString value;
    usize    index;
};

auto push_value(const ArgSpec& spec, MatchedArg& matched, OsString raw, usize index)
    -> Option<rstd::argparse::ParseError> {
    auto parsed = spec.parser->parse(raw.as_os_str());
    if (parsed.is_err()) {
        auto error = rstd::move(parsed).unwrap_err();
        if (error.is_InvalidUtf8()) {
            return Some(rstd::argparse::ParseError::InvalidUtf8Value(
                spec.id.clone(), rstd::move(raw), index));
        }
        return Some(rstd::argparse::ParseError::InvalidValue(
            spec.id.clone(), rstd::move(raw), index, rstd::move(error)));
    }
    matched.typed_values.push(rstd::move(parsed).unwrap());
    matched.raw_values.push(rstd::move(raw));
    matched.indices.push(usize(index));
    return None();
}

auto apply_flag(const ArgSpec& spec, MatchedArg& matched, usize index)
    -> Option<rstd::argparse::ParseError> {
    if (spec.action.is_SetTrue() || spec.action.is_SetFalse()) {
        if (matched.typed_values.is_empty()) {
            matched.typed_values.push(Box<dyn<rstd::any::Any>>::make(spec.action.is_SetTrue()));
        }
    } else if (spec.action.is_Count()) {
        if (matched.typed_values.is_empty()) {
            matched.typed_values.push(Box<dyn<rstd::any::Any>>::make(u8 { 1 }));
        } else {
            auto count = rstd::any::downcast_mut<u8>(matched.typed_values[0].deref_mut());
            if (count.is_none()) {
                return Some(rstd::argparse::ParseError::InvalidValue(
                    spec.id.clone(),
                    OsString::make(),
                    index,
                    rstd::argparse::ValueError::Message(
                        String::make("count argument has an invalid compiled type"))));
            }
            if (**count != rstd::numeric_limits<u8>::max()) ++**count;
        }
    }
    matched.indices.push(usize(index));
    ++matched.occurrences;
    matched.occurrence_ends.push(matched.typed_values.len());
    return None();
}

auto relation_present(const MatchedArg& matched) noexcept -> bool {
    return matched.occurrences != 0 && ! matched.from_default;
}

struct ParseRun {
    rstd::argparse::Matches matches;
    Vec<OsString>           unknown;
};

#define RSTD_ARGPARSE_RUN_OUTCOME_VARIANTS(V) \
    V(Parsed, (ParseRun value;))              \
    V(Display, (rstd::argparse::DisplayRequest value;))

class RunOutcome;

export namespace rstd::argparse
{

#define RSTD_ARGPARSE_DISPLAY_KIND_VARIANTS(V) \
    V(Help)                                    \
    V(Version)

RSTD_TAG_ENUM_WITH_DEFAULT(DisplayKind, RSTD_ARGPARSE_DISPLAY_KIND_VARIANTS, Help)

#undef RSTD_ARGPARSE_DISPLAY_KIND_VARIANTS

#define RSTD_ARGPARSE_OUTPUT_TARGET_VARIANTS(V) \
    V(Stdout)                                   \
    V(Stderr)

RSTD_TAG_ENUM_WITH_DEFAULT(OutputTarget, RSTD_ARGPARSE_OUTPUT_TARGET_VARIANTS, Stdout)

#undef RSTD_ARGPARSE_OUTPUT_TARGET_VARIANTS

class DisplayRequest {
    DisplayKind  kind_;
    String       text_;
    OutputTarget target_;
    i32          exit_code_;

public:
    DisplayRequest(DisplayKind kind, String text, OutputTarget target, i32 exit_code)
        : kind_(rstd::move(kind)),
          text_(rstd::move(text)),
          target_(rstd::move(target)),
          exit_code_(exit_code) {}

    [[nodiscard]]
    auto kind() const noexcept -> DisplayKind::Tag {
        return kind_.tag();
    }
    [[nodiscard]]
    auto text() const noexcept -> ref<str> {
        return text_.as_str();
    }
    [[nodiscard]]
    auto target() const noexcept -> OutputTarget::Tag {
        return target_.tag();
    }
    [[nodiscard]]
    auto exit_code() const noexcept -> i32 {
        return exit_code_;
    }
};

class ErrorReport {
    String       text_;
    OutputTarget target_;
    i32          exit_code_;

public:
    ErrorReport(String text, OutputTarget target, i32 exit_code)
        : text_(rstd::move(text)), target_(rstd::move(target)), exit_code_(exit_code) {}

    [[nodiscard]]
    auto text() const noexcept -> ref<str> {
        return text_.as_str();
    }
    [[nodiscard]]
    auto target() const noexcept -> OutputTarget::Tag {
        return target_.tag();
    }
    [[nodiscard]]
    auto exit_code() const noexcept -> i32 {
        return exit_code_;
    }
};

#define RSTD_ARGPARSE_PARSE_OUTCOME_VARIANTS(V) \
    V(Parsed, (T value;))                       \
    V(Display, (DisplayRequest request;))

template<typename T>
class ParseOutcome {
    RSTD_ENUM_BODY(ParseOutcome, RSTD_ARGPARSE_PARSE_OUTCOME_VARIANTS)
};

#undef RSTD_ARGPARSE_PARSE_OUTCOME_VARIANTS

class Parser {
    Arc<CompiledCommand> schema_;

    explicit Parser(Arc<CompiledCommand> schema): schema_(rstd::move(schema)) {}
    friend class Command;

    auto run(Vec<OsString> argv, bool known, usize index_offset) const
        -> Result<RunOutcome, ParseError>;
    auto run_impl(Vec<OsString> argv, bool known, usize index_offset, ref<str> display_path) const
        -> Result<RunOutcome, ParseError>;
    [[nodiscard]]
    auto render_usage_for(ref<str> display_path) const -> String;
    [[nodiscard]]
    auto render_help_for(ref<str> display_path) const -> String;
    [[nodiscard]]
    auto render_version_for(ref<str> display_path) const -> String;

public:
    Parser(const Parser&)            = delete;
    Parser& operator=(const Parser&) = delete;
    Parser(Parser&&)                 = default;
    Parser& operator=(Parser&&)      = default;

    [[nodiscard]]
    auto name() const noexcept -> ref<str> {
        return schema_->name.as_str();
    }
    [[nodiscard]]
    auto arg_count() const noexcept -> usize {
        return schema_->args.len();
    }
    [[nodiscard]]
    auto positional_count() const noexcept -> usize {
        return schema_->positionals.len();
    }
    [[nodiscard]]
    auto contains_arg(ref<str> id) const -> bool {
        return schema_->id_index.contains_key(id);
    }
    [[nodiscard]]
    auto contains_option(ref<str> name) const -> bool {
        return schema_->option_index.contains_key(name);
    }

    auto parse_from(Vec<OsString> argv) const -> Result<ParseOutcome<Matches>, ParseError>;
    auto parse_known_from(Vec<OsString> argv) const
        -> Result<ParseOutcome<KnownMatches>, ParseError>;
    template<typename I>
        requires Impled<I, iter::IntoIterator> && (! mtp::same_as<mtp::rm_cvf<I>, Vec<OsString>>)
    auto parse_from(I argv) const -> Result<ParseOutcome<Matches>, ParseError> {
        auto iterator = as<iter::IntoIterator>(argv).into_iter();
        static_assert(mtp::same_as<typename decltype(iterator)::Item, OsString>);
        auto values = Vec<OsString>::make();
        for (auto value = iterator.next(); value.is_some(); value = iterator.next()) {
            values.push(rstd::move(*value));
        }
        return parse_from(rstd::move(values));
    }
    template<typename I>
        requires Impled<I, iter::IntoIterator> && (! mtp::same_as<mtp::rm_cvf<I>, Vec<OsString>>)
    auto parse_known_from(I argv) const -> Result<ParseOutcome<KnownMatches>, ParseError> {
        auto iterator = as<iter::IntoIterator>(argv).into_iter();
        static_assert(mtp::same_as<typename decltype(iterator)::Item, OsString>);
        auto values = Vec<OsString>::make();
        for (auto value = iterator.next(); value.is_some(); value = iterator.next()) {
            values.push(rstd::move(*value));
        }
        return parse_known_from(rstd::move(values));
    }
    auto parse_env() const -> Result<ParseOutcome<Matches>, ParseError>;

    [[nodiscard]]
    auto render_usage() const -> String;
    [[nodiscard]]
    auto render_help() const -> String;
    [[nodiscard]]
    auto render_version() const -> String;
    [[nodiscard]]
    auto render_error(const ParseError& error) const -> ErrorReport;
};

} // namespace rstd::argparse

class RunOutcome {
    RSTD_ENUM_BODY(RunOutcome, RSTD_ARGPARSE_RUN_OUTCOME_VARIANTS)
};

#undef RSTD_ARGPARSE_RUN_OUTCOME_VARIANTS

void render_argument_line(String& output, const ArgSpec& argument) {
    output.push_str("  ");
    if (argument.short_name.is_some()) {
        output.push_back('-');
        output.push_back(*argument.short_name);
        if (argument.long_name.is_some()) output.push_str(", ");
    }
    if (argument.long_name.is_some()) {
        output.push_str("--");
        output.push_str(argument.long_name->as_str());
    }
    if (argument.short_name.is_none() && argument.long_name.is_none()) {
        output.push_str(argument.value_name.is_empty() ? argument.id.as_str()
                                                       : argument.value_name.as_str());
    }
    if (! argument.help.is_empty()) {
        output.push_str("\t");
        output.push_str(argument.help.as_str());
    }
    if (argument.default_raw_value.is_some()) {
        output.push_str(" [default: ");
        auto default_value = argument.default_raw_value->as_os_str().to_string_lossy();
        output.push_str(default_value.as_str());
        output.push_back(']');
    }
    if (! argument.possible_values.is_empty()) {
        output.push_str(" [possible values: ");
        for (usize value = 0; value < argument.possible_values.len(); ++value) {
            if (value != 0) output.push_str(", ");
            output.push_str(argument.possible_values[value].as_str());
        }
        output.push_back(']');
    }
    output.push_back('\n');
}

auto rstd::argparse::Parser::render_usage() const -> String {
    return render_usage_for(schema_->command_path.as_str());
}

auto rstd::argparse::Parser::render_usage_for(ref<str> display_path) const -> String {
    auto output = rstd::format("Usage: {}", display_path);
    if (! schema_->option_index.is_empty()) output.push_str(" [OPTIONS]");
    for (usize i = 0; i < schema_->positionals.len(); ++i) {
        const auto& argument = schema_->args[schema_->positionals[i]];
        if (! argument.required)
            output.push_str(" [");
        else
            output.push_str(" <");
        output.push_str(argument.value_name.is_empty() ? argument.id.as_str()
                                                       : argument.value_name.as_str());
        output.push_back(argument.required ? '>' : ']');
        if (argument.num_args.maximum().is_none() ||
            (argument.num_args.maximum().is_some() && *argument.num_args.maximum() > 1)) {
            output.push_str("...");
        }
    }
    if (! schema_->subcommands.is_empty()) {
        output.push_str(schema_->subcommand_required ? " <COMMAND>" : " [COMMAND]");
    }
    return output;
}

auto rstd::argparse::Parser::render_help() const -> String {
    return render_help_for(schema_->command_path.as_str());
}

auto rstd::argparse::Parser::render_help_for(ref<str> display_path) const -> String {
    auto output = String::make();
    if (schema_->long_about.is_some()) {
        output.push_str(schema_->long_about->as_str());
        output.push_str("\n\n");
    } else if (schema_->about.is_some()) {
        output.push_str(schema_->about->as_str());
        output.push_str("\n\n");
    }
    output.push_str(render_usage_for(display_path).as_str());
    output.push_str("\n");

    auto render_section = [&](ref<str> heading, bool options, Option<ref<str>> custom) {
        bool wrote_heading = false;
        for (usize i = 0; i < schema_->args.len(); ++i) {
            const auto& argument = schema_->args[i];
            if (argument.hidden) continue;
            const bool named = argument.short_name.is_some() || argument.long_name.is_some() ||
                               ! argument.short_aliases.is_empty() || ! argument.aliases.is_empty();
            const bool selected = custom.is_some()
                                      ? argument.help_heading.as_str() == *custom
                                      : argument.help_heading.is_empty() && named == options;
            if (! selected) continue;
            if (! wrote_heading) {
                output.push_str("\n");
                output.push_str(heading);
                output.push_str(":\n");
                wrote_heading = true;
            }
            render_argument_line(output, argument);
        }
    };

    render_section("Arguments", false, None());
    render_section("Options", true, None());
    for (usize i = 0; i < schema_->args.len(); ++i) {
        const auto& argument = schema_->args[i];
        if (argument.hidden || argument.help_heading.is_empty()) continue;
        bool already_rendered = false;
        for (usize previous = 0; previous < i; ++previous) {
            if (schema_->args[previous].help_heading.as_str() == argument.help_heading.as_str()) {
                already_rendered = true;
                break;
            }
        }
        if (! already_rendered) {
            render_section(
                argument.help_heading.as_str(), true, Some(argument.help_heading.as_str()));
        }
    }
    if (! schema_->subcommands.is_empty()) {
        output.push_str("\nSubcommands:\n");
        for (usize i = 0; i < schema_->subcommands.len(); ++i) {
            output.push_str("  ");
            output.push_str(schema_->subcommands[i].name.as_str());
            if (schema_->subcommands[i].schema->about.is_some()) {
                output.push_str("\t");
                output.push_str(schema_->subcommands[i].schema->about->as_str());
            }
            output.push_back('\n');
        }
    }
    if (schema_->after_help.is_some()) {
        output.push_str("\n");
        output.push_str(schema_->after_help->as_str());
        output.push_back('\n');
    }
    return output;
}

auto rstd::argparse::Parser::render_version() const -> String {
    return render_version_for(schema_->command_path.as_str());
}

auto rstd::argparse::Parser::render_version_for(ref<str> display_path) const -> String {
    if (schema_->version.is_none()) return String::make(display_path);
    return rstd::format("{} {}", display_path, *schema_->version);
}

auto rstd::argparse::Parser::render_error(const ParseError& error) const -> ErrorReport {
    auto usage = error.usage().size() == 0 ? render_usage() : String::make(error.usage());
    auto text  = rstd::format("error: {}\n\n{}\n", error, usage);
    return ErrorReport { rstd::move(text), OutputTarget::Stderr(), 2 };
}

auto rstd::argparse::Parser::run(Vec<OsString> argv, bool known, usize index_offset) const
    -> Result<RunOutcome, ParseError> {
    auto display_path =
        argv.is_empty() ? schema_->command_path.clone() : argv[0].as_os_str().to_string_lossy();
    auto result = run_impl(rstd::move(argv), known, index_offset, display_path.as_str());
    if (result.is_ok()) return result;
    auto error = rstd::move(result).unwrap_err();
    if (error.command_path().size() == 0) {
        error.attach_context(display_path.clone(), render_usage_for(display_path.as_str()));
    }
    return Err(rstd::move(error));
}

auto rstd::argparse::Parser::run_impl(Vec<OsString> argv,
                                      bool          known,
                                      usize         index_offset,
                                      ref<str>      display_path) const
    -> Result<RunOutcome, ParseError> {
    auto matched = Vec<MatchedArg>::with_capacity(schema_->args.len());
    for (usize slot = 0; slot < schema_->args.len(); ++slot) matched.push(MatchedArg {});
    auto                 unknown            = Vec<OsString>::make();
    Option<String>       subcommand_name    = None();
    Option<Box<Matches>> subcommand_matches = None();

    usize positional = 0;
    bool  options    = true;
    usize index      = argv.is_empty() ? 0 : 1;
    auto  absolute   = [index_offset](usize value) {
        return value + index_offset;
    };

    auto display = [&](const ArgSpec& spec) -> Option<RunOutcome> {
        if (spec.action.is_Help()) {
            return Some(RunOutcome::Display(DisplayRequest {
                DisplayKind::Help(), render_help_for(display_path), OutputTarget::Stdout(), 0 }));
        }
        if (spec.action.is_Version()) {
            return Some(RunOutcome::Display(DisplayRequest { DisplayKind::Version(),
                                                             render_version_for(display_path),
                                                             OutputTarget::Stdout(),
                                                             0 }));
        }
        return None();
    };

    auto consume_option =
        [&](usize slot, Option<IndexedValue> attached) -> Result<Option<RunOutcome>, ParseError> {
        const auto& spec  = schema_->args[slot];
        auto&       value = matched[slot];
        if (auto request = display(spec); request.is_some()) return Ok(rstd::move(request));

        if (! spec.action.is_Set() && ! spec.action.is_Append()) {
            if (attached.is_some()) {
                return Err(ParseError::TooManyValues(spec.id.clone(), 0, absolute(index)));
            }
            if (auto error = apply_flag(spec, value, absolute(index)); error.is_some()) {
                return Err(rstd::move(*error));
            }
            return Ok(None());
        }

        if (spec.action.is_Set() && value.occurrences != 0) {
            return Err(ParseError::DuplicateArgument(spec.id.clone(), absolute(index)));
        }

        usize count = 0;
        if (attached.is_some()) {
            if (spec.num_args.maximum().is_some() && *spec.num_args.maximum() == 0) {
                return Err(ParseError::TooManyValues(spec.id.clone(), 0, absolute(index)));
            }
            auto item = rstd::move(*attached);
            if (auto error = push_value(spec, value, rstd::move(item.value), item.index);
                error.is_some()) {
                return Err(rstd::move(*error));
            }
            ++count;
        }

        while (spec.num_args.maximum().is_none() || count < *spec.num_args.maximum()) {
            if (index + 1 >= argv.len()) break;
            auto candidate = argv[index + 1].as_os_str();
            if (! spec.allow_hyphen_values && is_option_like(candidate)) break;
            ++index;
            if (auto error = push_value(spec, value, clone_os(candidate), absolute(index));
                error.is_some()) {
                return Err(rstd::move(*error));
            }
            ++count;
        }

        if (count < spec.num_args.minimum()) {
            if (count == 0) {
                return Err(ParseError::MissingValue(spec.id.clone(), absolute(index)));
            }
            return Err(ParseError::TooFewValues(
                spec.id.clone(), spec.num_args.minimum(), count, absolute(index)));
        }
        if (count == 0 && spec.implicit_value.is_some()) {
            value.typed_values.push((*spec.implicit_value)->clone_value());
            value.raw_values.push(clone_os(spec.implicit_raw_value->as_os_str()));
            value.indices.push(absolute(index));
        }
        ++value.occurrences;
        value.occurrence_ends.push(value.typed_values.len());
        return Ok(None());
    };

    while (index < argv.len()) {
        auto token = argv[index].as_os_str();
        if (options && token.len() == 2 && token.data()[0] == '-' && token.data()[1] == '-') {
            options = false;
            ++index;
            continue;
        }

        if (options && token.len() > 2 && token.data()[0] == '-' && token.data()[1] == '-') {
            auto slot = lookup_option(*schema_, token);
            if (slot.is_none()) {
                if (known) {
                    unknown.push(clone_os(token));
                    ++index;
                    continue;
                }
                return Err(ParseError::UnknownArgument(
                    clone_os(token), absolute(index), suggest_option(*schema_, token)));
            }
            Option<IndexedValue> attached = None();
            if (auto split = token.split_once('='); split.is_some()) {
                attached =
                    Some(IndexedValue { clone_os(split->template get<1>()), absolute(index) });
            }
            auto result = consume_option(*slot, rstd::move(attached));
            if (result.is_err()) return Err(rstd::move(result).unwrap_err());
            if (result->is_some()) return Ok(rstd::move(**result));
            ++index;
            continue;
        }

        if (options && token.len() > 1 && token.data()[0] == '-') {
            auto exact = lookup_option(*schema_, token);
            if (exact.is_some()) {
                auto result = consume_option(*exact, None());
                if (result.is_err()) return Err(rstd::move(result).unwrap_err());
                if (result->is_some()) return Ok(rstd::move(**result));
                ++index;
                continue;
            }

            bool cluster_known = true;
            for (usize offset = 1; offset < token.len();) {
                char name[3] = { '-', static_cast<char>(token.data()[offset]), '\0' };
                auto slot    = schema_->option_index.get(ref<str>(name));
                if (slot.is_none()) {
                    cluster_known = false;
                    break;
                }
                const auto& spec = schema_->args[**slot];
                if (spec.action.is_Set() || spec.action.is_Append()) break;
                ++offset;
            }
            if (! cluster_known) {
                if (known) {
                    unknown.push(clone_os(token));
                    ++index;
                    continue;
                }
                return Err(ParseError::UnknownArgument(
                    clone_os(token), absolute(index), suggest_option(*schema_, token)));
            }

            for (usize offset = 1; offset < token.len(); ++offset) {
                char        name[3] = { '-', static_cast<char>(token.data()[offset]), '\0' };
                auto        slot    = schema_->option_index.get(ref<str>(name));
                const auto& spec    = schema_->args[**slot];
                Option<IndexedValue> attached = None();
                if ((spec.action.is_Set() || spec.action.is_Append()) && offset + 1 < token.len()) {
                    attached = Some(
                        IndexedValue { clone_os(ref<OsStr>::from_raw_parts(
                                           token.data() + offset + 1, token.len() - offset - 1)),
                                       absolute(index) });
                }
                auto result = consume_option(**slot, rstd::move(attached));
                if (result.is_err()) return Err(rstd::move(result).unwrap_err());
                if (result->is_some()) return Ok(rstd::move(**result));
                if (spec.action.is_Set() || spec.action.is_Append()) break;
            }
            ++index;
            continue;
        }

        if (options) {
            auto text            = token.to_str();
            auto subcommand_slot = text.is_some() ? schema_->subcommand_index.get(*text) : None();
            if (subcommand_slot.is_some()) {
                const auto& subcommand = schema_->subcommands[**subcommand_slot];
                auto        child_argv = Vec<OsString>::with_capacity(argv.len() - index);
                auto        child_name = token.to_string_lossy();
                child_argv.push(OsString::from(rstd::format("{} {}", display_path, child_name)));
                for (usize child_index = index + 1; child_index < argv.len(); ++child_index) {
                    child_argv.push(clone_os(argv[child_index].as_os_str()));
                }
                auto child_parser = Parser { subcommand.schema.clone() };
                auto child_result =
                    child_parser.run(rstd::move(child_argv), known, absolute(index));
                if (child_result.is_err()) {
                    return Err(rstd::move(child_result).unwrap_err());
                }
                auto child_outcome = rstd::move(child_result).unwrap();
                if (child_outcome.is_Display()) {
                    return Ok(RunOutcome::Display(rstd::move(child_outcome).as_Display().value));
                }
                auto child         = rstd::move(child_outcome).as_Parsed().value;
                auto child_unknown = rstd::move(child.unknown).into_iter();
                for (auto value = child_unknown.next(); value.is_some();
                     value      = child_unknown.next()) {
                    unknown.push(rstd::move(*value));
                }
                subcommand_name    = Some(subcommand.name.clone());
                subcommand_matches = Some(Box<Matches>::make(rstd::move(child.matches)));
                index              = argv.len();
                break;
            }
        }

        while (positional < schema_->positionals.len()) {
            auto slot    = schema_->positionals[positional];
            auto maximum = schema_->args[slot].num_args.maximum();
            if (maximum.is_some() && matched[slot].typed_values.len() >= *maximum) {
                ++positional;
                continue;
            }
            if (positional + 1 < schema_->positionals.len() &&
                matched[slot].typed_values.len() >= schema_->args[slot].num_args.minimum() &&
                count_positional_candidates(*schema_, argv, index, options) <=
                    schema_->positional_reserve[positional]) {
                ++positional;
                continue;
            }
            break;
        }
        if (positional == schema_->positionals.len()) {
            if (known) {
                unknown.push(clone_os(token));
                ++index;
                continue;
            }
            if (! schema_->subcommands.is_empty()) {
                return Err(ParseError::InvalidSubcommand(clone_os(token), absolute(index)));
            }
            return Err(ParseError::UnexpectedPositional(clone_os(token), absolute(index)));
        }
        auto slot = schema_->positionals[positional];
        if (auto error =
                push_value(schema_->args[slot], matched[slot], clone_os(token), absolute(index));
            error.is_some()) {
            return Err(rstd::move(*error));
        }
        matched[slot].occurrences = 1;
        if (matched[slot].occurrence_ends.is_empty()) {
            matched[slot].occurrence_ends.push(matched[slot].typed_values.len());
        } else {
            matched[slot].occurrence_ends[0] = matched[slot].typed_values.len();
        }
        ++index;
    }

    for (usize slot = 0; slot < schema_->args.len(); ++slot) {
        const auto& spec  = schema_->args[slot];
        auto&       value = matched[slot];
        if (value.occurrences == 0 && spec.default_value.is_some()) {
            value.typed_values.push((*spec.default_value)->clone_value());
            value.raw_values.push(clone_os(spec.default_raw_value->as_os_str()));
            value.indices.push(usize { 0 });
            value.occurrences  = 1;
            value.from_default = true;
            value.occurrence_ends.push(value.typed_values.len());
        }
        if (spec.required && value.occurrences == 0) {
            return Err(ParseError::MissingRequiredArgument(spec.id.clone()));
        }
        if ((spec.action.is_Set() || spec.action.is_Append()) && value.occurrences != 0 &&
            value.typed_values.len() < spec.num_args.minimum()) {
            return Err(ParseError::TooFewValues(spec.id.clone(),
                                                spec.num_args.minimum(),
                                                value.typed_values.len(),
                                                absolute(index)));
        }
    }

    for (usize group_slot = 0; group_slot < schema_->groups.len(); ++group_slot) {
        const auto& group   = schema_->groups[group_slot];
        usize       present = 0;
        usize       first   = 0;
        usize       second  = 0;
        for (usize member_index = 0; member_index < group.members.len(); ++member_index) {
            const usize member = group.members[member_index];
            if (! relation_present(matched[member])) continue;
            if (present == 0)
                first = member;
            else if (present == 1)
                second = member;
            ++present;
        }
        if (group.required && present == 0) {
            return Err(ParseError::MissingRequiredGroup(group.id.clone()));
        }
        if (! group.multiple && present > 1) {
            return Err(ParseError::ArgumentConflict(schema_->args[first].id.clone(),
                                                    schema_->args[second].id.clone()));
        }
    }

    for (usize relation = 0; relation < schema_->conflicts.len(); ++relation) {
        const auto& conflict = schema_->conflicts[relation];
        if (relation_present(matched[conflict.left]) && relation_present(matched[conflict.right])) {
            return Err(ParseError::ArgumentConflict(schema_->args[conflict.left].id.clone(),
                                                    schema_->args[conflict.right].id.clone()));
        }
    }

    for (usize relation = 0; relation < schema_->requirements.len(); ++relation) {
        const auto& requirement = schema_->requirements[relation];
        if (! relation_present(matched[requirement.source])) continue;
        if (! requirement.target_is_group) {
            if (! relation_present(matched[requirement.target])) {
                return Err(ParseError::MissingRequiredArgument(
                    schema_->args[requirement.target].id.clone()));
            }
            continue;
        }
        const auto& group   = schema_->groups[requirement.target];
        bool        present = false;
        for (usize member_index = 0; member_index < group.members.len(); ++member_index) {
            if (relation_present(matched[group.members[member_index]])) {
                present = true;
                break;
            }
        }
        if (! present) return Err(ParseError::MissingRequiredGroup(group.id.clone()));
    }

    if (schema_->subcommand_required && subcommand_matches.is_none()) {
        return Err(ParseError::MissingSubcommand(schema_->name.clone()));
    }

    auto matches = Matches { schema_.clone(),
                             rstd::move(matched),
                             rstd::move(subcommand_name),
                             rstd::move(subcommand_matches) };
    return Ok(RunOutcome::Parsed(ParseRun { rstd::move(matches), rstd::move(unknown) }));
}

auto rstd::argparse::Parser::parse_from(Vec<OsString> argv) const
    -> Result<ParseOutcome<Matches>, ParseError> {
    auto result = run(rstd::move(argv), false, 0);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err());
    auto outcome = rstd::move(result).unwrap();
    if (outcome.is_Display()) {
        return Ok(ParseOutcome<Matches>::Display(rstd::move(outcome).as_Display().value));
    }
    return Ok(ParseOutcome<Matches>::Parsed(rstd::move(outcome).as_Parsed().value.matches));
}

auto rstd::argparse::Parser::parse_known_from(Vec<OsString> argv) const
    -> Result<ParseOutcome<KnownMatches>, ParseError> {
    auto result = run(rstd::move(argv), true, 0);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err());
    auto outcome = rstd::move(result).unwrap();
    if (outcome.is_Display()) {
        return Ok(ParseOutcome<KnownMatches>::Display(rstd::move(outcome).as_Display().value));
    }
    auto run = rstd::move(outcome).as_Parsed().value;
    return Ok(ParseOutcome<KnownMatches>::Parsed(
        KnownMatches { rstd::move(run.matches), rstd::move(run.unknown) }));
}

auto rstd::argparse::Parser::parse_env() const -> Result<ParseOutcome<Matches>, ParseError> {
    auto argv = Vec<OsString>::make();
    auto args = rstd::env::args_os();
    for (auto value = args.next(); value.is_some(); value = args.next()) {
        argv.push(rstd::move(*value));
    }
    return parse_from(rstd::move(argv));
}
