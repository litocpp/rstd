import rstd.core;

using U128 = rstd::uint128_t;
using I128 = rstd::int128_t;
using U64  = rstd::uint64_t;

extern "C" {
I128 __multi3(I128, I128);
U128 __udivti3(U128, U128);
U128 __udivmodti4(U128, U128, U128*);
I128 __divti3(I128, I128);
I128 __modti3(I128, I128);
U128 __umodti3(U128, U128);
I128 __ashlti3(I128, int);
I128 __ashrti3(I128, int);
I128 __lshrti3(I128, int);
}

static U128 result;
static U128 remainder;

extern "C" [[clang::export_name("calculate")]]
void calculate(int operation, U64 low_a, U64 high_a, U64 low_b, U64 high_b, int shift) {
    const U128 a        = (U128(high_a) << 64) | low_a;
    const U128 b        = (U128(high_b) << 64) | low_b;
    const I128 signed_a = __builtin_bit_cast(I128, a);
    const I128 signed_b = __builtin_bit_cast(I128, b);
    remainder           = 0;
    switch (operation) {
    case 0: result = U128(__multi3(signed_a, signed_b)); break;
    case 1: result = __udivti3(a, b); break;
    case 2: result = __umodti3(a, b); break;
    case 3: result = __udivmodti4(a, b, &remainder); break;
    case 4: result = U128(__divti3(signed_a, signed_b)); break;
    case 5: result = U128(__modti3(signed_a, signed_b)); break;
    case 6: result = U128(__ashlti3(signed_a, shift)); break;
    case 7: result = U128(__ashrti3(signed_a, shift)); break;
    case 8: result = U128(__lshrti3(signed_a, shift)); break;
    case 9: result = a * b; break;
    case 10: result = a / b; break;
    case 11: result = a % b; break;
    }
}

extern "C" [[clang::export_name("result_low")]]
auto result_low() -> U64 {
    return U64(result);
}

extern "C" [[clang::export_name("result_high")]]
auto result_high() -> U64 {
    return U64(result >> 64);
}

extern "C" [[clang::export_name("remainder_low")]]
auto remainder_low() -> U64 {
    return U64(remainder);
}

extern "C" [[clang::export_name("remainder_high")]]
auto remainder_high() -> U64 {
    return U64(remainder >> 64);
}

extern "C" [[clang::export_name("panic_test")]]
void panic_test() {
    rstd::panic_message("runtime panic test", rstd::source_location::current());
}
