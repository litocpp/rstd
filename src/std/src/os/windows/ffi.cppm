module;
#include <rstd/macro.hpp>
export module rstd:os.windows.ffi;
import :ffi.os_str;
import :ffi.os_str.encoding;
import rstd.alloc;

using namespace rstd::prelude;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

#if RSTD_OS_WINDOWS
export namespace rstd::os::windows::ffi
{

class EncodeWide : public DefaultInClass<EncodeWide, iter::Iterator> {
    slice<u8> bytes_;
    usize     index_ {};
    unsigned  pending_ {};

public:
    using Item = u16;
    explicit EncodeWide(ref<OsStr> value [[clang::lifetimebound]])
        : bytes_(value.as_encoded_bytes()) {}
    auto next() -> Option<u16> {
        if (pending_ != 0) {
            auto result = pending_;
            pending_    = 0;
            return Some(u16(result));
        }
        if (index_ == bytes_.len()) return None();
        auto value = rstd::ffi::os_encoding::decode(bytes_, index_);
        if (value <= 0xffff) return Some(u16(value));
        value -= 0x10000;
        pending_ = 0xdc00 | (value & 0x3ff);
        return Some(u16(0xd800 | (value >> 10)));
    }
};

struct OsStringExt {
    static auto from_wide(slice<u16> value) -> OsString {
        return OsString::from_encoded_bytes_unchecked(rstd::ffi::os_encoding::from_wide(value));
    }
};

struct OsStrExt {
    static auto encode_wide(ref<OsStr> value [[clang::lifetimebound]]) -> EncodeWide {
        return EncodeWide(value);
    }
};

} // namespace rstd::os::windows::ffi
#endif
