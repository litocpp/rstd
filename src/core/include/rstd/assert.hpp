#pragma once

#ifdef NDEBUG
#    define debug_assert(...) ((void)0)
#    define debug_assert_eq(...) ((void)0)
#else
#    define debug_assert(EXP, ...) \
        if (! (EXP)) rstd::assert_fmt(#EXP __VA_OPT__(, ) __VA_ARGS__)
#    define debug_assert_eq(A, B) \
        if ((A) != (B)) rstd::assert_raw("(" #A " == " #B ")", {})
#endif

#define assert(EXP, ...) \
    if (! (EXP)) rstd::assert_fmt(#EXP __VA_OPT__(, ) __VA_ARGS__)
#define assert_eq(A, B) \
    if ((A) != (B)) rstd::assert_raw("(" #A " == " #B ")", {})
