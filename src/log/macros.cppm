export module rstd.log:macros;
export import :logger;
export import :record;
export import rstd.core;

namespace rstd::log
{

namespace detail
{

inline void
log_internal(Level level, ref<str> target, fmt::Arguments args, panic_::Location loc) noexcept {
    if (level <= max_level()) {
        Record rec { Metadata { level, target }, args, loc };
        log(rec);
    }
}

} // namespace detail

// ── error ─────────────────────────────────────────────────────────────────

export template<typename... Args>
struct error {
    error(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Error,
                             ref<str>(),
                             { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                             panic_::Location::from(loc.val));
    }
    error(Target                      tgt,
          fmt::format_string<Args...> fmt_str,
          Args&&... args,
          panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Error,
                             tgt.value,
                             { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                             panic_::Location::from(loc.val));
    }
};
template<>
struct error<> {
    error(fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Error,
                             ref<str>(),
                             { fmt_str.data(), fmt_str.size(), nullptr, 0 },
                             panic_::Location::from(loc.val));
    }
    error(Target tgt, fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Error,
                             tgt.value,
                             { fmt_str.data(), fmt_str.size(), nullptr, 0 },
                             panic_::Location::from(loc.val));
    }
};
template<typename... Args>
error(fmt::format_string<Args...>, Args&&...) -> error<Args...>;
template<typename... Args>
error(Target, fmt::format_string<Args...>, Args&&...) -> error<Args...>;

// ── warn ──────────────────────────────────────────────────────────────────

export template<typename... Args>
struct warn {
    warn(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Warn,
                             ref<str>(),
                             { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                             panic_::Location::from(loc.val));
    }
    warn(Target tgt, fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Warn,
                             tgt.value,
                             { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                             panic_::Location::from(loc.val));
    }
};
template<>
struct warn<> {
    warn(fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Warn,
                             ref<str>(),
                             { fmt_str.data(), fmt_str.size(), nullptr, 0 },
                             panic_::Location::from(loc.val));
    }
    warn(Target tgt, fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Warn,
                             tgt.value,
                             { fmt_str.data(), fmt_str.size(), nullptr, 0 },
                             panic_::Location::from(loc.val));
    }
};
template<typename... Args>
warn(fmt::format_string<Args...>, Args&&...) -> warn<Args...>;
template<typename... Args>
warn(Target, fmt::format_string<Args...>, Args&&...) -> warn<Args...>;

// ── info ──────────────────────────────────────────────────────────────────

export template<typename... Args>
struct info {
    info(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Info,
                             ref<str>(),
                             { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                             panic_::Location::from(loc.val));
    }
    info(Target tgt, fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Info,
                             tgt.value,
                             { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                             panic_::Location::from(loc.val));
    }
};
template<>
struct info<> {
    info(fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Info,
                             ref<str>(),
                             { fmt_str.data(), fmt_str.size(), nullptr, 0 },
                             panic_::Location::from(loc.val));
    }
    info(Target tgt, fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Info,
                             tgt.value,
                             { fmt_str.data(), fmt_str.size(), nullptr, 0 },
                             panic_::Location::from(loc.val));
    }
};
template<typename... Args>
info(fmt::format_string<Args...>, Args&&...) -> info<Args...>;
template<typename... Args>
info(Target, fmt::format_string<Args...>, Args&&...) -> info<Args...>;

// ── debug ─────────────────────────────────────────────────────────────────

export template<typename... Args>
struct debug {
    debug(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Debug,
                             ref<str>(),
                             { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                             panic_::Location::from(loc.val));
    }
    debug(Target                      tgt,
          fmt::format_string<Args...> fmt_str,
          Args&&... args,
          panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Debug,
                             tgt.value,
                             { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                             panic_::Location::from(loc.val));
    }
};
template<>
struct debug<> {
    debug(fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Debug,
                             ref<str>(),
                             { fmt_str.data(), fmt_str.size(), nullptr, 0 },
                             panic_::Location::from(loc.val));
    }
    debug(Target tgt, fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Debug,
                             tgt.value,
                             { fmt_str.data(), fmt_str.size(), nullptr, 0 },
                             panic_::Location::from(loc.val));
    }
};
template<typename... Args>
debug(fmt::format_string<Args...>, Args&&...) -> debug<Args...>;
template<typename... Args>
debug(Target, fmt::format_string<Args...>, Args&&...) -> debug<Args...>;

// ── trace ─────────────────────────────────────────────────────────────────

export template<typename... Args>
struct trace {
    trace(fmt::format_string<Args...> fmt_str, Args&&... args, panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Trace,
                             ref<str>(),
                             { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                             panic_::Location::from(loc.val));
    }
    trace(Target                      tgt,
          fmt::format_string<Args...> fmt_str,
          Args&&... args,
          panic_::SrcLoc loc = {}) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        detail::log_internal(Level::Trace,
                             tgt.value,
                             { fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) },
                             panic_::Location::from(loc.val));
    }
};
template<>
struct trace<> {
    trace(fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Trace,
                             ref<str>(),
                             { fmt_str.data(), fmt_str.size(), nullptr, 0 },
                             panic_::Location::from(loc.val));
    }
    trace(Target tgt, fmt::format_string<> fmt_str, panic_::SrcLoc loc = {}) {
        detail::log_internal(Level::Trace,
                             tgt.value,
                             { fmt_str.data(), fmt_str.size(), nullptr, 0 },
                             panic_::Location::from(loc.val));
    }
};
template<typename... Args>
trace(fmt::format_string<Args...>, Args&&...) -> trace<Args...>;
template<typename... Args>
trace(Target, fmt::format_string<Args...>, Args&&...) -> trace<Args...>;

} // namespace rstd::log
