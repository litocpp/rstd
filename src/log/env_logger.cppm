module;
#include <rstd/macro.hpp>
export module rstd.log:env_logger;
export import :logger;
export import :record;
export import rstd.core;
import rstd;

using namespace rstd::prelude;
using namespace rstd::log;

// ── Filter rule ───────────────────────────────────────────────────────────

struct FilterRule {
    char        target[64] {};
    u8          target_len { 0 };
    LevelFilter level { LevelFilter::Off };
};

// ── Color style ───────────────────────────────────────────────────────────

enum class Style : u8
{
    Auto,
    Always,
    Never
};

inline auto stderr_is_tty() noexcept -> bool {
    return rstd::sys::io::stdio::is_terminal_fd(2);
}

inline auto parse_style(ref<str> s) noexcept -> Style {
    auto eq_ci = [](ref<str> a, const char* b, usize n) {
        if (a.size() != n) return false;
        for (usize i = 0; i < n; ++i) {
            char ca = (char)a.data()[i];
            char cb = b[i];
            if (ca >= 'A' && ca <= 'Z') ca = char(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = char(cb - 'A' + 'a');
            if (ca != cb) return false;
        }
        return true;
    };
    if (eq_ci(s, "always", 6)) return Style::Always;
    if (eq_ci(s, "never", 5)) return Style::Never;
    return Style::Auto;
}

inline auto padded_level_str(Level l) noexcept -> const char* {
    switch (l) {
    case Level::Error: return "ERROR";
    case Level::Warn: return "WARN ";
    case Level::Info: return "INFO ";
    case Level::Debug: return "DEBUG";
    case Level::Trace: return "TRACE";
    }
    return "?????";
}

inline auto level_color(Level l) noexcept -> const char* {
    switch (l) {
    case Level::Error: return "\x1b[31m";
    case Level::Warn: return "\x1b[33m";
    case Level::Info: return "\x1b[32m";
    case Level::Debug: return "\x1b[34m";
    case Level::Trace: return "\x1b[36m";
    }
    return "";
}

inline constexpr char  COLOR_RESET[]    = "\x1b[0m";
inline constexpr usize COLOR_PREFIX_LEN = 5; // "\x1b[XYm"
inline constexpr usize COLOR_RESET_LEN  = 4; // "\x1b[0m"
inline constexpr usize PADDED_LEVEL_LEN = 5;

// Extract a "module path" (namespace prefix) from a pretty function string,
// matching Rust's module_path!() semantics: drop the trailing function name.
// Example: "void rstd::log::foo()" -> "rstd::log".
// Returns empty when the function has no namespace (e.g. "void foo()").
// Note: C++20 module names are not surfaced in __PRETTY_FUNCTION__; the
// linker-level "name@module.name" mangling is unavailable at compile time,
// so we use the namespace path instead, which is the closest analogue.
inline auto module_from_function(const char* fn) noexcept -> ref<str> {
    if (fn == nullptr || *fn == '\0') return {};
    const char* last_sep = nullptr;
    int         depth    = 0;
    for (const char* p = fn; *p != '\0'; ++p) {
        char c = *p;
        if (c == '<')
            ++depth;
        else if (c == '>')
            --depth;
        else if (depth == 0) {
            if (c == '(') break;
            if (c == ':' && p[1] == ':') {
                last_sep = p;
                ++p;
            }
        }
    }
    if (last_sep == nullptr) return {};
    const char* start = last_sep;
    while (start > fn && *(start - 1) != ' ') --start;
    if (start >= last_sep) return {};
    return ref<str>((const u8*)start, usize(last_sep - start));
}

namespace rstd::log
{

// ── StderrWriter ────────────────────────────────────────────────────────────

/// A raw stderr fd writer used by EnvLogger.
export struct StderrWriter {
    int fd = 2;
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
    static constexpr usize MAX_RULES = 16;

    FilterRule  rules[MAX_RULES];
    usize       rule_count { 0 };
    LevelFilter default_level { LevelFilter::Error };
    Style       style { Style::Auto };
    bool        color_enabled { false };

    EnvLogger() noexcept {
        parse_env();
        parse_style_env();
        color_enabled = (style == Style::Always) || (style == Style::Auto && stderr_is_tty());
    }

    explicit EnvLogger(ref<str> filters) noexcept { parse_filters(filters); }

    // ── filtering ─────────────────────────────────────────────────────────

    auto enabled(Metadata const& m) const noexcept -> bool {
        LevelFilter target_level = default_level;
        for (usize i = 0; i < rule_count; ++i) {
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
        for (usize i = 0; i < rule_count; ++i) {
            if (rules[i].level > max) max = rules[i].level;
        }
        return max;
    }

private:
    // ── parsing ───────────────────────────────────────────────────────────

    void parse_env() noexcept {
        auto* val = rstd::sys::getenv_internal("RSTD_LOG");
        if (val == nullptr) return;
        parse_filters(ref<str>(val));
    }

    void parse_style_env() noexcept {
        auto* val = rstd::sys::getenv_internal("RSTD_LOG_STYLE");
        if (val == nullptr) return;
        style = ::parse_style(ref<str>(val));
    }

    void parse_filters(ref<str> input) noexcept {
        const u8* p     = input.data();
        const u8* end   = p + input.size();
        const u8* token = p;

        while (p <= end) {
            if (p == end || *p == ',') {
                parse_one_rule(ref<str>(token, usize(p - token)));
                token = p + 1;
            }
            ++p;
        }
    }

    void parse_one_rule(ref<str> raw) noexcept {
        // trim spaces
        const u8* p   = raw.data();
        const u8* end = p + raw.size();
        while (p < end && (*p == ' ' || *p == '\t')) ++p;
        while (end > p && (*(end - 1) == ' ' || *(end - 1) == '\t')) --end;
        if (p >= end) return;

        // find '='
        const u8* eq = p;
        while (eq < end && *eq != '=') ++eq;

        if (eq < end) {
            // target=level
            const u8* t_end = eq;
            while (t_end > p && (*(t_end - 1) == ' ' || *(t_end - 1) == '\t')) --t_end;
            const u8* l_beg = eq + 1;
            while (l_beg < end && (*l_beg == ' ' || *l_beg == '\t')) ++l_beg;

            if (rule_count < MAX_RULES) {
                auto& rule = rules[rule_count++];
                auto  tlen = usize(t_end - p);
                if (tlen >= sizeof(rule.target)) tlen = sizeof(rule.target) - 1;
                __builtin_memcpy(rule.target, p, tlen);
                rule.target[tlen] = '\0';
                rule.target_len   = u8(tlen);
                rule.level        = parse_level_filter(ref<str>(l_beg, usize(end - l_beg)))
                                        .unwrap_or(LevelFilter::Trace);
            }
        } else {
            // global level directive
            auto lf = parse_level_filter(ref<str>(p, usize(end - p)));
            if (lf.is_some()) {
                default_level = lf.unwrap_unchecked();
            }
        }
    }

    static auto match_prefix(ref<str> target, const char* prefix, u8 prefix_len) noexcept -> bool {
        if (prefix_len == 0) return true;
        if (target.size() < prefix_len) return false;
        return __builtin_strncmp((char*)target.data(), prefix, prefix_len) == 0;
    }

    // ── formatting output ─────────────────────────────────────────────────

    void write_record(Record const& r) const noexcept {
        StderrWriter   w;
        fmt::Formatter f(&w, [](void* ctx, const u8* p, usize len) -> bool {
            auto* self = static_cast<StderrWriter*>(ctx);
            while (len > 0) {
                auto res = rstd::sys::io::stdio::write_fd(self->fd, p, len);
                if (res.is_err()) return false;
                auto n = res.unwrap_unchecked();
                if (n == 0) return false;
                p += n;
                len -= n;
            }
            return true;
        });

        f.write_raw((u8*)"[", 1);

        char ts[20];
        rstd::time::format_rfc3339_utc_now(ts);
        f.write_raw((u8 const*)ts, 20);
        f.write_raw((u8*)" ", 1);

        if (color_enabled) {
            f.write_raw((u8 const*)level_color(r.lvl()), COLOR_PREFIX_LEN);
        }
        f.write_raw((u8 const*)padded_level_str(r.lvl()), PADDED_LEVEL_LEN);
        if (color_enabled) {
            f.write_raw((u8 const*)COLOR_RESET, COLOR_RESET_LEN);
        }

        auto md = module_from_function(r.loc().function_name());
        if (md.size() > 0) {
            f.write_raw((u8*)" ", 1);
            f.write_raw(md.data(), md.size());
        }

        auto tgt = r.target();
        if (tgt.size() > 0) {
            f.write_raw((u8*)" ", 1);
            f.write_raw(tgt.data(), tgt.size());
        }

        f.write_raw((u8*)"] ", 2);

        r.args_().fmt(f);

        f.write_raw((u8*)"\n", 1);
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
