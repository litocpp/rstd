
#ifdef NDEBUG
#    define debug_assert(...)
#    define debug_assert_eq(...)
#else
#    define debug_assert(exp)     rstd::assert(exp)
#    define debug_assert_eq(a, b) rstd::assert((a) == (b))
#endif