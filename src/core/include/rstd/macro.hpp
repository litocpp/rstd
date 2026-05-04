#pragma once

#if defined(_WIN32)
#    define RSTD_OS_WINDOWS 1
#endif

#if defined(__linux__)
#    define RSTD_OS_LINUX 1
#endif

#if defined(__APPLE__)
#    define RSTD_OS_APPLE 1
#endif

#if defined(__unix__) || defined(__APPLE__)
#    define RSTD_OS_UNIX 1
#endif

#define USE_TRAIT(Class)                                                    \
    using Self = Class;                                                     \
    template<typename, typename>                                            \
    friend struct rstd::Impl;                                               \
    template<typename _USE_TRAIT_T>                                         \
        requires rstd::Impled<Self, rstd::cmp::PartialEq<_USE_TRAIT_T>>     \
    friend bool operator==(const Self& a, const _USE_TRAIT_T& b) noexcept { \
        return as<rstd::cmp::PartialEq<_USE_TRAIT_T>>(a).eq(b);             \
    }

#define RSTD_STR(a, ...)   #a
#define RSTD_FIRST(a, ...) a
#define RSTD_REST(a, ...)  __VA_ARGS__

#define RSTD_REST_ARGS(a, ...) __VA_OPT__(, ) __VA_ARGS__

#ifdef NDEBUG
#    define debug_assert(...)    ((void)0)
#    define debug_assert_eq(...) ((void)0)
#else
#    define debug_assert(EXP, ...) \
        if (! (EXP)) rstd::panic(#EXP RSTD_STR(__VA_ARGS__) RSTD_REST_ARGS(__VA_ARGS__))
#    define debug_assert_eq(A, B) debug_assert((A) == (B))
#endif

#define assert(EXP, ...) \
    if (! (EXP)) rstd::panic(#EXP RSTD_STR(__VA_ARGS__) RSTD_REST_ARGS(__VA_ARGS__))
#define assert_eq(A, B) assert((A) == (B))

#define rstd_error(...)                                            \
    do {                                                           \
        if (rstd::log::log_enabled(rstd::log::Level::Error, "")) { \
            rstd::log::error(__VA_ARGS__);                         \
        }                                                          \
    } while (0)

#define rstd_warn(...)                                            \
    do {                                                          \
        if (rstd::log::log_enabled(rstd::log::Level::Warn, "")) { \
            rstd::log::warn(__VA_ARGS__);                         \
        }                                                         \
    } while (0)

#define rstd_info(...)                                            \
    do {                                                          \
        if (rstd::log::log_enabled(rstd::log::Level::Info, "")) { \
            rstd::log::info(__VA_ARGS__);                         \
        }                                                         \
    } while (0)

#ifdef NDEBUG
#    define rstd_debug(...) ((void)0)
#else
#    define rstd_debug(...)                                            \
        do {                                                           \
            if (rstd::log::log_enabled(rstd::log::Level::Debug, "")) { \
                rstd::log::debug(__VA_ARGS__);                         \
            }                                                          \
        } while (0)
#endif

#define rstd_trace(...)                                            \
    do {                                                           \
        if (rstd::log::log_enabled(rstd::log::Level::Trace, "")) { \
            rstd::log::trace(__VA_ARGS__);                         \
        }                                                          \
    } while (0)
