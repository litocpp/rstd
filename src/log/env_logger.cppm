export module rstd.log:env_logger;
export import :logger;
export import :record;
export import rstd.core;
import rstd;

using namespace rstd::prelude;
using namespace rstd::log;
using namespace rstd::literals;

// ── Filter rule ───────────────────────────────────────────────────────────

struct FilterRule {
    byte         target[64] {};
    rstd::size_t target_len { 0 };
    LevelFilter  level { LevelFilter::Off };
};

// ── Color style ───────────────────────────────────────────────────────────

enum class Style : rstd::uint8_t
{
    Auto,
    Always,
    Never
};

inline auto stderr_is_tty() noexcept -> bool {
    return rstd::io::stderr().is_terminal();
}

inline auto parse_style(ref<str> s) noexcept -> Style {
    auto eq_ci = [](ref<str> a, ref<str> b) {
        if (a.size() != b.size()) return false;
        for (rstd::size_t i = 0; i < a.size().to_primitive(); ++i) {
            auto ca = a[usize(i)].to_primitive();
            auto cb = b[usize(i)].to_primitive();
            if (ca >= 'A' && ca <= 'Z') ca = static_cast<rstd::uint8_t>(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = static_cast<rstd::uint8_t>(cb - 'A' + 'a');
            if (ca != cb) return false;
        }
        return true;
    };
    if (eq_ci(s, "always"_str)) return Style::Always;
    if (eq_ci(s, "never"_str)) return Style::Never;
    return Style::Auto;
}

inline auto padded_level_str(Level l) noexcept -> ref<str> {
    switch (l) {
    case Level::Error: return "ERROR"_str;
    case Level::Warn: return "WARN "_str;
    case Level::Info: return "INFO "_str;
    case Level::Debug: return "DEBUG"_str;
    case Level::Trace: return "TRACE"_str;
    }
    return "?????"_str;
}

inline auto level_color(Level l) noexcept -> ref<str> {
    switch (l) {
    case Level::Error: return "\x1b[31m"_str;
    case Level::Warn: return "\x1b[33m"_str;
    case Level::Info: return "\x1b[32m"_str;
    case Level::Debug: return "\x1b[34m"_str;
    case Level::Trace: return "\x1b[36m"_str;
    }
    return ""_str;
}

inline constexpr auto COLOR_RESET = "\x1b[0m"_str;

namespace rstd::log
{

// ── StderrWriter ────────────────────────────────────────────────────────────

/// A raw stderr fd writer used by EnvLogger.
struct StderrWriter {
    rstd::io::Stderr stderr;
};

// ── EnvLogger ─────────────────────────────────────────────────────────────

/// A simple logger configured via the `RSTD_LOG` environment variable.
///
/// Format: `RSTD_LOG=[target][=][level][,...]`
/// Examples:
///   `RSTD_LOG=debug`            — global debug level
///   `RSTD_LOG=my_module=trace`  — trace for my_module, error elsewhere
///   `RSTD_LOG=debug,my_module=off` — global debug, my_module disabled
///
/// Target matching uses prefix search (e.g. `foo` matches `foo`, `foo::bar`).
export struct EnvLogger {
    static constexpr rstd::size_t MAX_RULES = 16;

    FilterRule   rules[MAX_RULES];
    rstd::size_t rule_count { 0 };
    LevelFilter  default_level { LevelFilter::Error };
    Style        style { Style::Auto };
    bool         color_enabled { false };

    EnvLogger() noexcept {
        parse_env();
        parse_style_env();
        color_enabled = (style == Style::Always) || (style == Style::Auto && stderr_is_tty());
    }

    explicit EnvLogger(ref<str> filters) noexcept { parse_filters(filters); }

    // ── filtering ─────────────────────────────────────────────────────────

    auto enabled(Metadata const& m) const noexcept -> bool {
        LevelFilter target_level = default_level;
        for (rstd::size_t i = 0; i < rule_count; ++i) {
            if (match_prefix(m.target, rules[i].target, rules[i].target_len)) {
                target_level = rules[i].level;
                break;
            }
        }
        return m.level <= target_level;
    }

    auto log(Record const& r) const noexcept -> void {
        if (! enabled(r.metadata)) return;
        write_record(r);
    }

    auto flush() const noexcept -> void {}

    // ── access ────────────────────────────────────────────────────────────

    auto filter() const noexcept -> LevelFilter {
        LevelFilter max = default_level;
        for (rstd::size_t i = 0; i < rule_count; ++i) {
            if (rules[i].level > max) max = rules[i].level;
        }
        return max;
    }

private:
    // ── parsing ───────────────────────────────────────────────────────────

    void parse_env() noexcept {
        auto val = rstd::env::var("RSTD_LOG"_str);
        if (val.is_none()) return;
        parse_filters(val->as_str());
    }

    void parse_style_env() noexcept {
        auto val = rstd::env::var("RSTD_LOG_STYLE"_str);
        if (val.is_none()) return;
        style = ::parse_style(val->as_str());
    }

    void parse_filters(ref<str> input) noexcept {
        const byte* p     = input.data();
        const byte* end   = p + input.size().to_primitive();
        const byte* token = p;

        while (p <= end) {
            if (p == end || u8::from_byte(*p) == u8(',')) {
                parse_one_rule(ref<str>::from_raw_parts_unchecked(token, usize(p - token)));
                token = p + 1;
            }
            ++p;
        }
    }

    void parse_one_rule(ref<str> raw) noexcept {
        // trim spaces
        const byte* p   = raw.data();
        const byte* end = p + raw.size().to_primitive();
        while (p < end && (u8::from_byte(*p) == u8(' ') || u8::from_byte(*p) == u8('\t'))) ++p;
        while (end > p &&
               (u8::from_byte(*(end - 1)) == u8(' ') || u8::from_byte(*(end - 1)) == u8('\t')))
            --end;
        if (p >= end) return;

        // find '='
        const byte* eq = p;
        while (eq < end && u8::from_byte(*eq) != u8('=')) ++eq;

        if (eq < end) {
            // target=level
            const byte* t_end = eq;
            while (t_end > p && (u8::from_byte(*(t_end - 1)) == u8(' ') ||
                                 u8::from_byte(*(t_end - 1)) == u8('\t')))
                --t_end;
            const byte* l_beg = eq + 1;
            while (l_beg < end &&
                   (u8::from_byte(*l_beg) == u8(' ') || u8::from_byte(*l_beg) == u8('\t')))
                ++l_beg;

            if (rule_count < MAX_RULES) {
                auto& rule = rules[rule_count++];
                auto  tlen = static_cast<rstd::size_t>(t_end - p);
                if (tlen >= sizeof(rule.target)) tlen = sizeof(rule.target) - 1;
                __builtin_memcpy(rule.target, p, tlen);
                rule.target[tlen] = byte {};
                rule.target_len   = tlen;
                rule.level        = parse_level_filter(
                                        ref<str>::from_raw_parts_unchecked(l_beg, usize(end - l_beg)))
                                        .unwrap_or(LevelFilter::Trace);
            }
        } else {
            // global level directive
            auto lf = parse_level_filter(ref<str>::from_raw_parts_unchecked(p, usize(end - p)));
            if (lf.is_some()) {
                default_level = lf.unwrap_unchecked();
            }
        }
    }

    static auto match_prefix(ref<str> target, const byte* prefix, rstd::size_t prefix_len) noexcept
        -> bool {
        if (prefix_len == 0) return true;
        if (target.size().to_primitive() < prefix_len) return false;
        return __builtin_memcmp(target.data(), prefix, prefix_len) == 0;
    }

    // ── formatting output ─────────────────────────────────────────────────

    void write_record(Record const& r) const noexcept {
        StderrWriter   w;
        fmt::Formatter f(&w, [](void* ctx, const rstd::uint8_t* p, rstd::size_t len) -> bool {
            auto* self  = static_cast<StderrWriter*>(ctx);
            auto  bytes = slice<u8>::from_raw_parts(reinterpret_cast<byte const*>(p), usize(len));
            while (! bytes.is_empty()) {
                auto res = rstd::as<rstd::io::Write>(self->stderr).write(bytes);
                if (res.is_err()) return false;
                auto count = res.unwrap_unchecked().to_primitive();
                if (count == 0 || count > bytes.len().to_primitive()) return false;
                bytes = slice<u8>::from_raw_parts(bytes.as_raw_ptr() + count,
                                                  usize(bytes.len().to_primitive() - count));
            }
            return true;
        });

        f.write_str("["_str);

        char ts[20];
        rstd::time::format_rfc3339_utc_now(ts);
        f.write_raw(ts, rstd::size_t(20));
        f.write_str(" "_str);

        if (color_enabled) {
            f.write_str(level_color(r.lvl()));
        }
        f.write_str(padded_level_str(r.lvl()));
        if (color_enabled) {
            f.write_str(COLOR_RESET);
        }

        auto tgt = r.target();
        if (tgt.size() > usize()) {
            f.write_str(" "_str);
            f.write_str(tgt);
        }

        f.write_str("] "_str);

        r.args_().fmt(f);

        f.write_str("\n"_str);
    }
};

} // namespace rstd::log

// ── Impl<Log, EnvLogger> ─────────────────────────────────────────────────
namespace rstd
{

template<>
struct Impl<log::Log, log::EnvLogger> : ImplBase<log::EnvLogger> {
    auto enabled(log::Metadata const& m) const -> bool { return this->self().enabled(m); }
    auto log(log::Record const& r) const -> void { this->self().log(r); }
    auto flush() const -> void { this->self().flush(); }
};

} // namespace rstd
