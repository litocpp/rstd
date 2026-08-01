module;
#include <rstd/macro.hpp>
export module rstd.core:ffi.c_str;
import :num.types;
import :error.trait;
export import :marker;
export import :str.traits;

namespace rstd::ffi
{

export class CStr;

export class FromBytesWithNulError {
public:
    enum class Kind : rstd::uint8_t
    {
        InteriorNul,
        NotNulTerminated,
    };

    static constexpr auto interior_nul(usize position) noexcept -> FromBytesWithNulError {
        return FromBytesWithNulError(Kind::InteriorNul, position);
    }

    static constexpr auto not_nul_terminated() noexcept -> FromBytesWithNulError {
        return FromBytesWithNulError(Kind::NotNulTerminated, usize());
    }

    constexpr auto kind() const noexcept -> Kind { return kind_; }
    constexpr auto nul_position() const noexcept -> Option<usize> {
        if (kind_ == Kind::InteriorNul) return Some(usize(position_.to_primitive()));
        return None();
    }

private:
    constexpr FromBytesWithNulError(Kind kind, usize position) noexcept
        : kind_(kind), position_(position) {}

    Kind  kind_;
    usize position_;
};

} // namespace rstd::ffi

using rstd::ffi::CStr;

namespace rstd
{

namespace c_str_error_detail
{
auto write_usize(fmt::Formatter& formatter, usize value) -> bool {
    char buffer[32];
    auto size = rstd::size_t();
    auto raw  = value.to_primitive();
    do {
        buffer[size++] = static_cast<char>('0' + raw % 10);
        raw /= 10;
    } while (raw != 0);
    for (rstd::size_t left = 0, right = size - 1; left < right; ++left, --right) {
        auto value    = buffer[left];
        buffer[left]  = buffer[right];
        buffer[right] = value;
    }
    return formatter.write_raw(buffer, size);
}
} // namespace c_str_error_detail

template<>
struct Impl<fmt::Display, ffi::FromBytesWithNulError> : ImplBase<ffi::FromBytesWithNulError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self().kind() == ffi::FromBytesWithNulError::Kind::InteriorNul) {
            constexpr char prefix[] =
                "data provided contains an interior nul byte at byte position ";
            if (! formatter.write_raw(prefix, sizeof(prefix) - 1)) return false;
            return c_str_error_detail::write_usize(formatter, *this->self().nul_position());
        }
        constexpr char message[] = "data provided is not nul terminated";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Debug, ffi::FromBytesWithNulError> : ImplBase<ffi::FromBytesWithNulError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self().kind() == ffi::FromBytesWithNulError::Kind::InteriorNul) {
            constexpr char prefix[] = "InteriorNul { position: ";
            if (! formatter.write_raw(prefix, sizeof(prefix) - 1)) return false;
            if (! c_str_error_detail::write_usize(formatter, *this->self().nul_position()))
                return false;
            return formatter.write_raw(" }", 2);
        }
        constexpr char message[] = "NotNulTerminated";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<error::Error, ffi::FromBytesWithNulError> : ImplBase<ffi::FromBytesWithNulError> {
    auto source() const noexcept -> Option<error::ErrorRef> { return None(); }
};

template<>
struct Impl<Sized, CStr> {
    ~Impl() = delete;
};

template<>
struct Impl<ptr_::Pointee, CStr> {
    using Metadata = usize;
};

template<>
struct ref<CStr> {
    USE_TRAIT(ref)

    char const* p { nullptr };
    usize       length {};

    constexpr auto count_bytes() const noexcept -> usize { return length; }
    constexpr auto is_empty() const noexcept -> bool { return length == usize(); }
    constexpr auto as_ptr() const noexcept [[clang::lifetimebound]] -> char const* { return p; }
    constexpr auto as_raw_ptr() const noexcept [[clang::lifetimebound]] -> char const* { return p; }
    constexpr auto metadata() const noexcept -> usize { return length; }

    auto to_bytes() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        return slice<u8>::from_raw_parts(reinterpret_cast<byte const*>(p), length);
    }

    auto to_bytes_with_nul() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        return slice<u8>::from_raw_parts(reinterpret_cast<byte const*>(p), length + usize(1));
    }

    auto to_str() const noexcept -> Result<ref<str>, str_::Utf8Error> {
        return str_::from_utf8(to_bytes());
    }

    constexpr auto deref() const noexcept -> ref<CStr> { return *this; }

    static constexpr auto from_raw_parts_unchecked(char const* data [[clang::lifetimebound]],
                                                   usize       len) noexcept -> ref<CStr> {
        return { .p = data, .length = len };
    }
};

template<>
struct mut_ref<CStr> {
    USE_TRAIT(mut_ref)

    char* p { nullptr };
    usize length {};

    constexpr auto count_bytes() const noexcept -> usize { return length; }
    constexpr auto is_empty() const noexcept -> bool { return length == usize(); }
    constexpr auto as_ptr() const noexcept [[clang::lifetimebound]] -> char const* { return p; }
    constexpr auto as_mut_ptr() noexcept [[clang::lifetimebound]] -> char* { return p; }
    constexpr auto as_raw_ptr() const noexcept [[clang::lifetimebound]] -> char* { return p; }
    constexpr auto metadata() const noexcept -> usize { return length; }

    constexpr auto deref() const noexcept -> ref<CStr> {
        return ref<CStr>::from_raw_parts_unchecked(p, length);
    }
    constexpr auto deref_mut() noexcept -> mut_ref<CStr> { return *this; }
};

namespace ffi
{

class CStr {
public:
    CStr()  = delete;
    ~CStr() = delete;

    static auto from_ptr(char const* data [[clang::lifetimebound]]) noexcept -> ref<CStr> {
        return from_ptr_with_nul_unchecked(data, usize(rstd::strlen(data)));
    }

    static constexpr auto from_ptr_with_nul_unchecked(char const* data [[clang::lifetimebound]],
                                                      usize       length) noexcept -> ref<CStr> {
        return ref<CStr>::from_raw_parts_unchecked(data, length);
    }

    static constexpr auto from_chars_with_nul(slice<char> chars [[clang::lifetimebound]]) noexcept
        -> Result<ref<CStr>, FromBytesWithNulError> {
        auto const len = chars.len().to_primitive();
        if (len == 0 || chars[usize(len - 1)] != '\0') {
            return Err(FromBytesWithNulError::not_nul_terminated());
        }
        for (rstd::size_t index = 0; index + 1 < len; ++index) {
            if (chars[usize(index)] == '\0') {
                return Err(FromBytesWithNulError::interior_nul(usize(index)));
            }
        }
        return Ok(from_ptr_with_nul_unchecked(chars.as_raw_ptr(), usize(len - 1)));
    }
};

} // namespace ffi

} // namespace rstd
