module;
#include <rstd/macro.hpp>
#include <stdlib.h>
#if RSTD_OS_UNIX
#  include <unistd.h>
#  include <errno.h>
#elif RSTD_OS_WINDOWS
#  include <windows.h>
#endif
export module rstd.log:env_logger;
export import :logger;
export import :record;
export import rstd.core;

namespace rstd::log
{

// ── Filter rule ───────────────────────────────────────────────────────────

struct FilterRule {
    char        target[64] {};
    u8          target_len { 0 };
    LevelFilter level { LevelFilter::Off };
};

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

    EnvLogger() noexcept { parse_env(); }

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
        if (!enabled(r.metadata)) return;
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
        auto* val = ::getenv("RSTD_LOG");
        if (val == nullptr) return;
        parse_filters(ref<str>(val));
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
                rule.target_len = u8(tlen);
                rule.level = parse_level_filter(ref<str>(l_beg, usize(end - l_beg)))
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
        StderrWriter w;
        fmt::Formatter f(&w, [](void* ctx, const u8* p, usize len) -> bool {
            auto* self = static_cast<StderrWriter*>(ctx);
            while (len > 0) {
#if RSTD_OS_UNIX
                auto n = ::write(self->fd, p, len);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    return false;
                }
                if (n == 0) return false;
                p   += n;
                len -= n;
#elif RSTD_OS_WINDOWS
                HANDLE h = GetStdHandle(self->fd == 2 ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
                if (h == INVALID_HANDLE_VALUE || h == nullptr) return false;
                DWORD written = 0;
                if (!WriteFile(h, p, static_cast<DWORD>(len), &written, nullptr)) return false;
                if (written == 0) return false;
                p   += written;
                len -= written;
#else
                return false;
#endif
            }
            return true;
        });

        // [
        f.write_raw((u8*)"[", 1);

        // LEVEL
        auto lvl_str = as_str(r.lvl());
        f.write_raw(lvl_str.data(), lvl_str.size());

        // target
        auto tgt = r.target();
        if (tgt.size() > 0) {
            f.write_raw((u8*)" ", 1);
            f.write_raw(tgt.data(), tgt.size());
        }

        // ]
        f.write_raw((u8*)"] ", 2);

        // message
        r.args_().fmt(f);

        // newline
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
