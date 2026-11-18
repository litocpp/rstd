module;
#include <rstd/enum.hpp>

export module rstd.argparse:parser;
export import :matches;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::collections::BTreeMap;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;
using rstd::sync::Arc;

class RunOutcome;

export namespace rstd::argparse
{

class DisplayKind final {
    RSTD_ENUM_DEFAULT(DisplayKind, (Help), (Help), (Version))
};

class OutputTarget final {
    RSTD_ENUM_DEFAULT(OutputTarget, (Stdout), (Stdout), (Stderr))
};

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

template<typename T>
class ParseOutcome {
    RSTD_ENUM(ParseOutcome, (Parsed, (T value;)), (Display, (DisplayRequest request;)))
};

class Parser {
    Arc<CompiledCommand> schema_;

    explicit Parser(Arc<CompiledCommand> schema): schema_(rstd::move(schema)) {}
    friend class Command;

    auto run(Vec<OsString> argv, bool known, usize index_offset) const
        -> Result<RunOutcome, ParseError>;
    auto run_with_globals(Vec<OsString>      argv,
                          bool               known,
                          usize              index_offset,
                          Arc<GlobalMatches> global_matches) const
        -> Result<RunOutcome, ParseError>;
    auto run_impl(Vec<OsString>      argv,
                  bool               known,
                  usize              index_offset,
                  ref<str>           display_path,
                  Arc<GlobalMatches> global_matches) const -> Result<RunOutcome, ParseError>;
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
        requires iter::into_iterable<I> &&
                 mtp::same_as<typename iter::into_iter_t<I>::Item, OsString> &&
                 (! mtp::same_as<mtp::rm_cvf<I>, Vec<OsString>>)
    auto parse_from(I argv) const -> Result<ParseOutcome<Matches>, ParseError> {
        auto values = iter::from_iter<Vec<OsString>>(rstd::move(argv));
        return parse_from(rstd::move(values));
    }
    template<typename I>
        requires iter::into_iterable<I> &&
                 mtp::same_as<typename iter::into_iter_t<I>::Item, OsString> &&
                 (! mtp::same_as<mtp::rm_cvf<I>, Vec<OsString>>)
    auto parse_known_from(I argv) const -> Result<ParseOutcome<KnownMatches>, ParseError> {
        auto values = iter::from_iter<Vec<OsString>>(rstd::move(argv));
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
