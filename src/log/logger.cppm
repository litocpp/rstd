export module rstd.log:logger;
export import :record;
export import rstd.core;

namespace rstd::log
{

// ── Log trait ─────────────────────────────────────────────────────────────

/// The trait that logging implementations must satisfy.
///
/// Mirrors Rust's log::Log trait.
export struct Log {
    using Trait                  = Log;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = Log;
        auto enabled(Metadata const&) const -> bool;
        auto log(Record const&) const -> void;
        auto flush() const -> void;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::enabled, &T::log, &T::flush>;
};

} // namespace rstd::log

using namespace rstd::prelude;
using namespace rstd::log;

// Nop logger — default when no logger is set.
struct NopLogger {};

inline bool nop_enabled(void const*, Metadata const&) {
    return false;
}
inline void nop_log(void const*, Record const&) {
}
inline void nop_flush(void const*) {
}

// Type-erased logger via function-pointer table (avoids vtable portability issues).
struct LoggerVTable {
    bool (*enabled)(void const*, Metadata const&);
    void (*log)(void const*, Record const&);
    void (*flush)(void const*);
};

inline constexpr LoggerVTable NOP_VTABLE { nop_enabled, nop_log, nop_flush };

// Three states: 0=uninitialized, 1=initializing, 2=initialized
inline rstd::sync::atomic::Atomic<usize> g_state { 0 };
inline rstd::sync::atomic::Atomic<usize> g_max_level { 0 }; // LevelFilter::Off by default

inline LoggerVTable g_vtable { nop_enabled, nop_log, nop_flush };
inline void const*  g_logger_ptr { nullptr };

namespace rstd::log
{

// ── set_logger / set_max_level ────────────────────────────────────────────

/// Returns the current global maximum log level.
export [[nodiscard]]
inline auto max_level() noexcept -> LevelFilter {
    return static_cast<LevelFilter>(g_max_level.load(sync::atomic::Ordering::Relaxed));
}

/// Sets the global maximum log level (relaxed ordering).
export inline void set_max_level(LevelFilter level) noexcept {
    g_max_level.store(static_cast<usize>(level), sync::atomic::Ordering::Relaxed);
}

/// Attempts to set the global logger. Returns false if already initialized.
///
/// This is a one-shot operation; subsequent calls return false.
export template<typename T>
    requires Impled<T, Log>
inline bool set_logger(T const& logger) noexcept {
    using ImplT = Impl<Log, T>;

    constexpr LoggerVTable vtable {
        [](void const* p, Metadata const& m) -> bool {
            return ImplT { const_cast<T*>(static_cast<T const*>(p)) }.enabled(m);
        },
        [](void const* p, Record const& r) -> void {
            ImplT { const_cast<T*>(static_cast<T const*>(p)) }.log(r);
        },
        [](void const* p) -> void {
            ImplT { const_cast<T*>(static_cast<T const*>(p)) }.flush();
        }
    };

    usize expected = 0;
    if (! g_state.compare_exchange_strong(
            expected, 1, sync::atomic::Ordering::Acquire, sync::atomic::Ordering::Relaxed)) {
        return false;
    }

    g_vtable     = vtable;
    g_logger_ptr = rstd::addressof(logger);
    g_state.store(2, sync::atomic::Ordering::Release);
    return true;
}

/// Returns true if the given level/target would be logged at the current max_level.
export [[nodiscard]]
inline auto log_enabled(Level level, ref<str> target) noexcept -> bool {
    if (level > max_level()) return false;
    if (g_state.load(sync::atomic::Ordering::Acquire) != 2) return false;
    return g_vtable.enabled(g_logger_ptr, Metadata { level, target });
}

/// Logs a Record through the global logger (no-op if no logger set).
export inline void log(Record const& record) noexcept {
    if (record.lvl() <= max_level()) {
        // Acquire ensures we see the fully initialized vtable from set_logger().
        if (g_state.load(sync::atomic::Ordering::Acquire) == 2) {
            g_vtable.log(g_logger_ptr, record);
        }
    }
}

/// Flushes any buffered records through the global logger.
export inline void flush() noexcept {
    if (g_state.load(sync::atomic::Ordering::Acquire) == 2) {
        g_vtable.flush(g_logger_ptr);
    }
}

} // namespace rstd::log

// ── Impl<Log, NopLogger> (default) ────────────────────────────────────────
namespace rstd
{

template<>
struct Impl<log::Log, NopLogger> : ImplBase<NopLogger> {
    auto enabled(log::Metadata const&) const -> bool { return false; }
    auto log(log::Record const&) const -> void {}
    auto flush() const -> void {}
};

} // namespace rstd
