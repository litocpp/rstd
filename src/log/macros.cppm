export module rstd.log:macros;
export import :logger;
export import :record;
export import rstd.core;

namespace rstd::log
{

namespace detail
{

/// Internal: creates a Record and dispatches to the global logger.
inline void log_internal(Level level, fmt::Arguments args, panic_::Location loc) noexcept {
    if (level <= max_level()) {
        Record rec {
            Metadata { level, ref<str>() },
            args,
            loc
        };
        log(rec);
    }
}

} // namespace detail

// ── error ─────────────────────────────────────────────────────────────────

/// Logs a message at the Error level.
export template<typename... Args>
struct error {
    error(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Error,
            { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
            panic_::Location::from(loc.val));
    }
};
template<>
struct error<> {
    error(fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Error,
            { fmt_str.data(), fmt_str.size(), nullptr, 0 },
            panic_::Location::from(loc.val));
    }
};
template<typename... Args>
error(fmt::format_string<Args...>, Args&&...) -> error<Args...>;

// ── warn ──────────────────────────────────────────────────────────────────

/// Logs a message at the Warn level.
export template<typename... Args>
struct warn {
    warn(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Warn,
            { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
            panic_::Location::from(loc.val));
    }
};
template<>
struct warn<> {
    warn(fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Warn,
            { fmt_str.data(), fmt_str.size(), nullptr, 0 },
            panic_::Location::from(loc.val));
    }
};
template<typename... Args>
warn(fmt::format_string<Args...>, Args&&...) -> warn<Args...>;

// ── info ──────────────────────────────────────────────────────────────────

/// Logs a message at the Info level.
export template<typename... Args>
struct info {
    info(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Info,
            { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
            panic_::Location::from(loc.val));
    }
};
template<>
struct info<> {
    info(fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Info,
            { fmt_str.data(), fmt_str.size(), nullptr, 0 },
            panic_::Location::from(loc.val));
    }
};
template<typename... Args>
info(fmt::format_string<Args...>, Args&&...) -> info<Args...>;

// ── debug ─────────────────────────────────────────────────────────────────

/// Logs a message at the Debug level.
export template<typename... Args>
struct debug {
    debug(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Debug,
            { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
            panic_::Location::from(loc.val));
    }
};
template<>
struct debug<> {
    debug(fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Debug,
            { fmt_str.data(), fmt_str.size(), nullptr, 0 },
            panic_::Location::from(loc.val));
    }
};
template<typename... Args>
debug(fmt::format_string<Args...>, Args&&...) -> debug<Args...>;

// ── trace ─────────────────────────────────────────────────────────────────

/// Logs a message at the Trace level.
export template<typename... Args>
struct trace {
    trace(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Trace,
            { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
            panic_::Location::from(loc.val));
    }
};
template<>
struct trace<> {
    trace(fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Trace,
            { fmt_str.data(), fmt_str.size(), nullptr, 0 },
            panic_::Location::from(loc.val));
    }
};
template<typename... Args>
trace(fmt::format_string<Args...>, Args&&...) -> trace<Args...>;

} // namespace rstd::log
