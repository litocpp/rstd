#pragma once

#define USE_TRAIT(Class)                                                    \
    using Self = Class;                                                     \
    template<typename, typename>                                            \
    friend struct rstd::Impl;                                               \
    template<typename _USE_TRAIT_T>                                         \
        requires Impled<Self, cmp::PartialEq<_USE_TRAIT_T>>                 \
    friend bool operator==(const Self& a, const _USE_TRAIT_T& b) noexcept { \
        return as<cmp::PartialEq<_USE_TRAIT_T>>(a).eq(b);                   \
    }

#ifdef NDEBUG
#    define debug_assert(...)    ((void)0)
#    define debug_assert_eq(...) ((void)0)
#else
#    define debug_assert(EXP, ...) \
        if (! (EXP)) rstd::assert_fmt(#EXP __VA_OPT__(, ) __VA_ARGS__)
#    define debug_assert_eq(A, B) \
        if ((A) != (B)) rstd::assert_fmt("(" #A " == " #B ")")
#endif

#define assert(EXP, ...) \
    if (! (EXP)) rstd::assert_fmt(#EXP __VA_OPT__(, ) __VA_ARGS__)
#define assert_eq(A, B) \
    if ((A) != (B)) rstd::assert_fmt("(" #A " == " #B ")")
