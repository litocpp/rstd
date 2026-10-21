export module rstd.tests.ffi_core_check;
import rstd.core;

using namespace rstd::prelude;
using rstd::ffi::CStr;
using rstd::ffi::FromBytesUntilNulError;

static_assert(Impled<FromBytesUntilNulError, rstd::fmt::Display>);
static_assert(Impled<FromBytesUntilNulError, rstd::fmt::Debug>);
static_assert(Impled<FromBytesUntilNulError, rstd::error::Error>);

constexpr auto check_ffi_core() -> bool {
    constexpr char input[] { 'a', '\0', 'b', '\0' };
    auto           chars  = slice<char>::from_raw_parts(input, usize(4));
    auto           result = CStr::from_chars_until_nul(chars);
    if (result.is_err()) return false;
    auto view = result.unwrap();
    if (view.as_ptr() != input || view.count_bytes() != usize(1)) return false;
    if (CStr::from_chars_with_nul(chars).is_ok()) return false;
    if (CStr::from_chars_until_nul(slice<char> {}).is_ok()) return false;
    if (CStr::from_chars_until_nul(slice<char>::from_raw_parts(input, usize(1))).is_ok())
        return false;
    auto empty = CStr::from_chars_until_nul(slice<char>::from_raw_parts(input + 1, usize(3)));
    return empty.is_ok() && empty.unwrap().is_empty();
}

static_assert(check_ffi_core());
static_assert(noexcept(CStr::from_chars_until_nul(slice<char> {})));

export auto ffi_core_check() -> bool {
    return check_ffi_core();
}
