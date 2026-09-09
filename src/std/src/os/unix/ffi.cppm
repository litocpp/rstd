module;
#include <rstd/macro.hpp>
export module rstd:os.unix.ffi;
import :ffi.os_str;
import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::vec::Vec;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

#if RSTD_OS_UNIX
export namespace rstd::os::unix::ffi
{

struct OsStringExt {
    static auto from_vec(Vec<u8> bytes) -> OsString {
        return OsString::from_encoded_bytes_unchecked(rstd::move(bytes));
    }
    static auto into_vec(OsString value) -> Vec<u8> {
        return rstd::move(value).into_encoded_bytes();
    }
};

struct OsStrExt {
    static constexpr auto from_bytes(slice<u8> bytes [[clang::lifetimebound]]) -> ref<OsStr> {
        return ref<OsStr>::from_encoded_bytes_unchecked(bytes);
    }
    static constexpr auto as_bytes(ref<OsStr> value [[clang::lifetimebound]]) -> slice<u8> {
        return value.as_encoded_bytes();
    }
};

} // namespace rstd::os::unix::ffi
#endif
