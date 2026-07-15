export module rstd.log:level;
export import rstd.core;

namespace rstd::log
{

/// The available verbosity levels for logging, ordered from most severe to least severe.
///
/// Discriminant values align with LevelFilter: Error=1, Warn=2, Info=3, Debug=4, Trace=5.
/// Off=0 exists only in LevelFilter.
export enum class Level : u32 {
    Error = 1,
    Warn  = 2,
    Info  = 3,
    Debug = 4,
    Trace = 5,
};

/// A verbosity level filter that includes Off (disable all logging).
export enum class LevelFilter : u32 {
    Off   = 0,
    Error = 1,
    Warn  = 2,
    Info  = 3,
    Debug = 4,
    Trace = 5,
};

} // namespace rstd::log

using namespace rstd::prelude;

inline constexpr char const* LEVEL_NAMES[] = { "OFF", "ERROR", "WARN", "INFO", "DEBUG", "TRACE" };

namespace rstd::log
{

/// Returns the uppercase string name for a Level.
export [[nodiscard]]
inline constexpr auto as_str(Level l) noexcept -> ref<str> {
    return LEVEL_NAMES[static_cast<u32>(l)];
}

/// Returns the uppercase string name for a LevelFilter.
export [[nodiscard]]
inline constexpr auto as_str(LevelFilter f) noexcept -> ref<str> {
    return LEVEL_NAMES[static_cast<u32>(f)];
}

// ── comparisons ───────────────────────────────────────────────────────────

export [[nodiscard]]
inline constexpr bool operator<=(Level l, LevelFilter f) noexcept {
    return static_cast<u32>(l) <= static_cast<u32>(f);
}

export [[nodiscard]]
inline constexpr bool operator<=(LevelFilter f, Level l) noexcept {
    return static_cast<u32>(f) <= static_cast<u32>(l);
}

export [[nodiscard]]
inline constexpr bool operator<(Level l, LevelFilter f) noexcept {
    return static_cast<u32>(l) < static_cast<u32>(f);
}

export [[nodiscard]]
inline constexpr bool operator<(LevelFilter f, Level l) noexcept {
    return static_cast<u32>(f) < static_cast<u32>(l);
}

export [[nodiscard]]
inline constexpr bool operator>=(Level l, LevelFilter f) noexcept {
    return static_cast<u32>(l) >= static_cast<u32>(f);
}

export [[nodiscard]]
inline constexpr bool operator>(Level l, LevelFilter f) noexcept {
    return static_cast<u32>(l) > static_cast<u32>(f);
}

export [[nodiscard]]
inline constexpr bool operator==(Level l, LevelFilter f) noexcept {
    return static_cast<u32>(l) == static_cast<u32>(f);
}

export [[nodiscard]]
inline constexpr bool operator!=(Level l, LevelFilter f) noexcept {
    return static_cast<u32>(l) != static_cast<u32>(f);
}

// ── conversions ───────────────────────────────────────────────────────────

/// Converts Level to its equivalent LevelFilter.
export [[nodiscard]]
inline constexpr auto to_level_filter(Level l) noexcept -> LevelFilter {
    return static_cast<LevelFilter>(static_cast<u32>(l));
}

/// Converts LevelFilter to Level, returning None if Off.
export [[nodiscard]]
inline constexpr auto to_level(LevelFilter f) noexcept -> Option<Level> {
    if (f == LevelFilter::Off) return None();
    return Some(static_cast<Level>(static_cast<u32>(f)));
}

// ── parse ─────────────────────────────────────────────────────────────────

/// Parses a level name (case-insensitive). Returns None on failure.
export [[nodiscard]]
inline constexpr auto parse_level(ref<str> s) noexcept -> Option<Level> {
    if (s.size() < 3 || s.size() > 5) return None();
    // Case-insensitive compare against known names
    auto cmp = [](ref<str> a, ref<str> b) -> bool {
        if (a.size() != b.size()) return false;
        for (usize i = 0; i < a.size(); ++i) {
            char ca = a.data()[i];
            char cb = b.data()[i];
            if (ca >= 'a' && ca <= 'z') ca = char(ca - 'a' + 'A');
            if (cb >= 'a' && cb <= 'z') cb = char(cb - 'a' + 'A');
            if (ca != cb) return false;
        }
        return true;
    };
    if (cmp(s, "ERROR")) return Some(Level::Error);
    if (cmp(s, "WARN")) return Some(Level::Warn);
    if (cmp(s, "INFO")) return Some(Level::Info);
    if (cmp(s, "DEBUG")) return Some(Level::Debug);
    if (cmp(s, "TRACE")) return Some(Level::Trace);
    return None();
}

/// Parses a level filter name (case-insensitive). Returns None on failure.
export [[nodiscard]]
inline constexpr auto parse_level_filter(ref<str> s) noexcept -> Option<LevelFilter> {
    if (s.size() < 2 || s.size() > 5) return None();
    auto cmp = [](ref<str> a, ref<str> b) -> bool {
        if (a.size() != b.size()) return false;
        for (usize i = 0; i < a.size(); ++i) {
            char ca = a.data()[i];
            char cb = b.data()[i];
            if (ca >= 'a' && ca <= 'z') ca = char(ca - 'a' + 'A');
            if (cb >= 'a' && cb <= 'z') cb = char(cb - 'a' + 'A');
            if (ca != cb) return false;
        }
        return true;
    };
    if (cmp(s, "OFF")) return Some(LevelFilter::Off);
    if (cmp(s, "ERROR")) return Some(LevelFilter::Error);
    if (cmp(s, "WARN")) return Some(LevelFilter::Warn);
    if (cmp(s, "INFO")) return Some(LevelFilter::Info);
    if (cmp(s, "DEBUG")) return Some(LevelFilter::Debug);
    if (cmp(s, "TRACE")) return Some(LevelFilter::Trace);
    return None();
}

} // namespace rstd::log

// ── fmt::Display ──────────────────────────────────────────────────────────
namespace rstd
{

template<>
struct Impl<fmt::Display, log::Level> : ImplBase<log::Level> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = log::as_str(this->self());
        return f.write_raw(s.data(), s.size());
    }
};

template<>
struct Impl<fmt::Display, log::LevelFilter> : ImplBase<log::LevelFilter> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = log::as_str(this->self());
        return f.write_raw(s.data(), s.size());
    }
};

} // namespace rstd
