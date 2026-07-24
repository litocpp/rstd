export module rstd.log:level;
export import rstd.core;

namespace rstd::log
{

/// The available verbosity levels for logging, ordered from most severe to least severe.
///
/// Discriminant values align with LevelFilter: Error=1, Warn=2, Info=3, Debug=4, Trace=5.
/// Off=0 exists only in LevelFilter.
export enum class Level : rstd::uint32_t {
    Error = 1,
    Warn  = 2,
    Info  = 3,
    Debug = 4,
    Trace = 5,
};

/// A verbosity level filter that includes Off (disable all logging).
export enum class LevelFilter : rstd::uint32_t {
    Off   = 0,
    Error = 1,
    Warn  = 2,
    Info  = 3,
    Debug = 4,
    Trace = 5,
};

} // namespace rstd::log

using namespace rstd::prelude;
using namespace rstd::literals;

inline constexpr ref<str> LEVEL_NAMES[] = { "OFF"_str,  "ERROR"_str, "WARN"_str,
                                            "INFO"_str, "DEBUG"_str, "TRACE"_str };

namespace rstd::log
{

/// Returns the uppercase string name for a Level.
export [[nodiscard]]
inline auto as_str(Level l) noexcept -> ref<str> {
    return LEVEL_NAMES[static_cast<rstd::uint32_t>(l)];
}

/// Returns the uppercase string name for a LevelFilter.
export [[nodiscard]]
inline auto as_str(LevelFilter f) noexcept -> ref<str> {
    return LEVEL_NAMES[static_cast<rstd::uint32_t>(f)];
}

// ── comparisons ───────────────────────────────────────────────────────────

export [[nodiscard]]
inline constexpr bool operator<=(Level l, LevelFilter f) noexcept {
    return static_cast<rstd::uint32_t>(l) <= static_cast<rstd::uint32_t>(f);
}

export [[nodiscard]]
inline constexpr bool operator<=(LevelFilter f, Level l) noexcept {
    return static_cast<rstd::uint32_t>(f) <= static_cast<rstd::uint32_t>(l);
}

export [[nodiscard]]
inline constexpr bool operator<(Level l, LevelFilter f) noexcept {
    return static_cast<rstd::uint32_t>(l) < static_cast<rstd::uint32_t>(f);
}

export [[nodiscard]]
inline constexpr bool operator<(LevelFilter f, Level l) noexcept {
    return static_cast<rstd::uint32_t>(f) < static_cast<rstd::uint32_t>(l);
}

export [[nodiscard]]
inline constexpr bool operator>=(Level l, LevelFilter f) noexcept {
    return static_cast<rstd::uint32_t>(l) >= static_cast<rstd::uint32_t>(f);
}

export [[nodiscard]]
inline constexpr bool operator>(Level l, LevelFilter f) noexcept {
    return static_cast<rstd::uint32_t>(l) > static_cast<rstd::uint32_t>(f);
}

export [[nodiscard]]
inline constexpr bool operator==(Level l, LevelFilter f) noexcept {
    return static_cast<rstd::uint32_t>(l) == static_cast<rstd::uint32_t>(f);
}

export [[nodiscard]]
inline constexpr bool operator!=(Level l, LevelFilter f) noexcept {
    return static_cast<rstd::uint32_t>(l) != static_cast<rstd::uint32_t>(f);
}

// ── conversions ───────────────────────────────────────────────────────────

/// Converts Level to its equivalent LevelFilter.
export [[nodiscard]]
inline constexpr auto to_level_filter(Level l) noexcept -> LevelFilter {
    return static_cast<LevelFilter>(static_cast<rstd::uint32_t>(l));
}

/// Converts LevelFilter to Level, returning None if Off.
export [[nodiscard]]
inline constexpr auto to_level(LevelFilter f) noexcept -> Option<Level> {
    if (f == LevelFilter::Off) return None();
    return Some(static_cast<Level>(static_cast<rstd::uint32_t>(f)));
}

// ── parse ─────────────────────────────────────────────────────────────────

/// Parses a level name (case-insensitive). Returns None on failure.
export [[nodiscard]]
inline constexpr auto parse_level(ref<str> s) noexcept -> Option<Level> {
    if (s.size() < usize(3) || s.size() > usize(5)) return None();
    // Case-insensitive compare against known names
    auto cmp = [](ref<str> a, ref<str> b) -> bool {
        if (a.size() != b.size()) return false;
        for (rstd::size_t i = 0; i < a.size().to_primitive(); ++i) {
            auto ca = a[usize(i)].to_primitive();
            auto cb = b[usize(i)].to_primitive();
            if (ca >= 'a' && ca <= 'z') ca = static_cast<rstd::uint8_t>(ca - 'a' + 'A');
            if (cb >= 'a' && cb <= 'z') cb = static_cast<rstd::uint8_t>(cb - 'a' + 'A');
            if (ca != cb) return false;
        }
        return true;
    };
    if (cmp(s, "ERROR"_str)) return Some(Level::Error);
    if (cmp(s, "WARN"_str)) return Some(Level::Warn);
    if (cmp(s, "INFO"_str)) return Some(Level::Info);
    if (cmp(s, "DEBUG"_str)) return Some(Level::Debug);
    if (cmp(s, "TRACE"_str)) return Some(Level::Trace);
    return None();
}

/// Parses a level filter name (case-insensitive). Returns None on failure.
export [[nodiscard]]
inline constexpr auto parse_level_filter(ref<str> s) noexcept -> Option<LevelFilter> {
    if (s.size() < usize(2) || s.size() > usize(5)) return None();
    auto cmp = [](ref<str> a, ref<str> b) -> bool {
        if (a.size() != b.size()) return false;
        for (rstd::size_t i = 0; i < a.size().to_primitive(); ++i) {
            auto ca = a[usize(i)].to_primitive();
            auto cb = b[usize(i)].to_primitive();
            if (ca >= 'a' && ca <= 'z') ca = static_cast<rstd::uint8_t>(ca - 'a' + 'A');
            if (cb >= 'a' && cb <= 'z') cb = static_cast<rstd::uint8_t>(cb - 'a' + 'A');
            if (ca != cb) return false;
        }
        return true;
    };
    if (cmp(s, "OFF"_str)) return Some(LevelFilter::Off);
    if (cmp(s, "ERROR"_str)) return Some(LevelFilter::Error);
    if (cmp(s, "WARN"_str)) return Some(LevelFilter::Warn);
    if (cmp(s, "INFO"_str)) return Some(LevelFilter::Info);
    if (cmp(s, "DEBUG"_str)) return Some(LevelFilter::Debug);
    if (cmp(s, "TRACE"_str)) return Some(LevelFilter::Trace);
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
        return f.write_str(s);
    }
};

template<>
struct Impl<fmt::Display, log::LevelFilter> : ImplBase<log::LevelFilter> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = log::as_str(this->self());
        return f.write_str(s);
    }
};

} // namespace rstd
