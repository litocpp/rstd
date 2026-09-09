module;
#include <rstd/macro.hpp>
export module rstd:ffi.os_str.platform;
import rstd.alloc;
import :ffi.os_str.encoding;

using namespace rstd::prelude;
using ::alloc::vec::Vec;

namespace rstd::ffi::os_string_platform
{

#if RSTD_OS_WINDOWS
inline constexpr bool USE_WTF8 = true;
#else
inline constexpr bool USE_WTF8 = false;
#endif

struct Slice {
    byte const*           data {};
    usize                 length {};
    static constexpr auto from_encoded_bytes_unchecked(slice<u8> bytes) noexcept -> Slice {
        return { bytes.as_raw_ptr(), bytes.len() };
    }
    constexpr auto as_encoded_bytes() const noexcept -> slice<u8> {
        return slice<u8>::from_raw_parts(data, length);
    }
};

class Buf {
    Vec<u8> inner_;

public:
    Buf() = default;
    explicit Buf(Vec<u8>&& bytes): inner_(rstd::move(bytes)) {}
    static auto from_encoded_bytes_unchecked(Vec<u8>&& bytes) -> Buf {
        return Buf(rstd::move(bytes));
    }
    auto as_slice() const noexcept [[clang::lifetimebound]] -> Slice {
        return Slice::from_encoded_bytes_unchecked(inner_.as_slice());
    }
    auto into_inner() && -> Vec<u8> { return rstd::move(inner_); }
    auto len() const noexcept -> usize { return inner_.len(); }
    auto capacity() const noexcept -> usize { return inner_.capacity(); }
    void clear() { inner_.clear(); }
    void reserve(usize count) { inner_.reserve(count); }
    void reserve_exact(usize count) { inner_.reserve_exact(count); }
    auto try_reserve(usize count) { return inner_.try_reserve(count); }
    auto try_reserve_exact(usize count) { return inner_.try_reserve_exact(count); }
    void shrink_to_fit() { inner_.shrink_to_fit(); }
    void shrink_to(usize capacity) { inner_.shrink_to(capacity); }
    void truncate(usize length) {
#if RSTD_OS_WINDOWS
        auto valid =
            length <= inner_.len() &&
            (length == inner_.len() || (inner_.as_slice()[length].to_primitive() & 0xc0) != 0x80);
#else
        auto valid = os_encoding::public_boundary(inner_.as_slice(), length);
#endif
        if (! valid) rstd::panic { "OsString::truncate invalid encoded boundary" };
        inner_.truncate(length);
    }
    void push(slice<u8> value) {
        // A borrowed view may alias this buffer across a reserve or surrogate join.
        if (! inner_.is_empty() && ! value.is_empty()) {
            auto start   = reinterpret_cast<rstd::uintptr_t>(inner_.data());
            auto address = reinterpret_cast<rstd::uintptr_t>(value.as_raw_ptr());
            if (address >= start && address - start < inner_.len().to_primitive()) {
                auto copy = Vec<u8>::from(value);
                push(copy.as_slice());
                return;
            }
        }
#if RSTD_OS_WINDOWS
        os_encoding::append_wtf8(inner_, value);
#else
        inner_.extend_from_slice(value);
#endif
    }
    void ascii_case(bool uppercase) {
        for (auto index = usize(); index < inner_.len(); ++index) {
            auto value = inner_.as_slice()[index].to_primitive();
            if (uppercase ? value >= 'a' && value <= 'z' : value >= 'A' && value <= 'Z')
                inner_[index] = u8(uppercase ? value - 32 : value + 32);
        }
    }
};

} // namespace rstd::ffi::os_string_platform
