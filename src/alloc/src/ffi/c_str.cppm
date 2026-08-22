module;
#include <bit>
#include <rstd/macro.hpp>
export module rstd.alloc:ffi.c_str;
export import :boxed;
export import :vec;
import :string;

using ::alloc::boxed::Box;
using ::alloc::string::String;
using ::alloc::vec::Vec;
using rstd::ffi::CStr;
using namespace rstd::prelude;

namespace alloc::ffi
{

export class CString;

export class NulError {
    usize   position_;
    Vec<u8> bytes_;

public:
    NulError(usize position, Vec<u8>&& bytes) noexcept
        : position_(position), bytes_(rstd::move(bytes)) {}

    auto nul_position() const noexcept -> usize { return position_; }
    auto as_bytes() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        return bytes_.as_slice();
    }
    auto into_vec() && -> Vec<u8> { return rstd::move(bytes_); }
};

export class FromVecWithNulError {
    rstd::ffi::FromBytesWithNulError error_;
    Vec<u8>                          bytes_;

public:
    FromVecWithNulError(rstd::ffi::FromBytesWithNulError error, Vec<u8>&& bytes) noexcept
        : error_(error), bytes_(rstd::move(bytes)) {}

    auto error() const noexcept -> rstd::ffi::FromBytesWithNulError { return error_; }
    auto as_bytes() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        return bytes_.as_slice();
    }
    auto into_bytes() && -> Vec<u8> { return rstd::move(bytes_); }
};

export class IntoStringError;

/// An owned, C-compatible string with one trailing nul and no interior nul.
export class CString {
    Box<char[]> inner;

    static auto copy_with_trailing_nul(slice<u8> bytes) -> Box<char[]> {
        auto chars = Vec<char>::with_capacity(bytes.len() + usize(1));
        for (rstd::size_t index = 0; index < bytes.len().to_primitive(); ++index) {
            u8 value = bytes[usize(index)];
            chars.push(std::bit_cast<char>(value.to_primitive()));
        }
        chars.push('\0');
        return chars.into_boxed_slice();
    }

public:
    USE_TRAIT(CString)

    explicit CString(Box<char[]>&& value) noexcept: inner(rstd::move(value)) {}
    CString(CString&&) noexcept            = default;
    CString& operator=(CString&&) noexcept = default;

    static auto from_vec_unchecked(Vec<u8>&& bytes) -> CString {
        return CString(copy_with_trailing_nul(bytes.as_slice()));
    }

    static auto make(Vec<u8>&& bytes) -> Result<CString, NulError> {
        for (rstd::size_t index = 0; index < bytes.len().to_primitive(); ++index) {
            if (bytes[usize(index)] == u8()) {
                return Err(NulError(usize(index), rstd::move(bytes)));
            }
        }
        return Ok(from_vec_unchecked(rstd::move(bytes)));
    }

    template<Impled<Into<Vec<u8>>> T>
    static auto make(T value) -> Result<CString, NulError> {
        return make(Vec<u8>(rstd::into(rstd::move(value))));
    }

    static auto from_vec_with_nul(Vec<u8>&& bytes) -> Result<CString, FromVecWithNulError> {
        auto const len = bytes.len().to_primitive();
        if (len == 0 || bytes[usize(len - 1)] != u8()) {
            auto error = rstd::ffi::FromBytesWithNulError::not_nul_terminated();
            return Err(FromVecWithNulError(error, rstd::move(bytes)));
        }
        for (rstd::size_t index = 0; index + 1 < len; ++index) {
            if (bytes[usize(index)] == u8()) {
                auto error = rstd::ffi::FromBytesWithNulError::interior_nul(usize(index));
                return Err(FromVecWithNulError(error, rstd::move(bytes)));
            }
        }

        auto chars = Vec<char>::with_capacity(bytes.len());
        for (rstd::size_t index = 0; index < bytes.len().to_primitive(); ++index) {
            u8 value = bytes[usize(index)];
            chars.push(std::bit_cast<char>(value.to_primitive()));
        }
        return Ok(CString(chars.into_boxed_slice()));
    }

    static auto from_raw_parts(char const* value) -> CString {
        auto const length = rstd::strlen(value);
        auto       chars  = Vec<char>::with_capacity(usize(length + 1));
        for (rstd::size_t index = 0; index <= length; ++index) {
            chars.emplace_back(value[index]);
        }
        return CString(chars.into_boxed_slice());
    }

    auto as_ref() const noexcept [[clang::lifetimebound]] -> ref<CStr> {
        return CStr::from_ptr_with_nul_unchecked(inner.as_ptr().as_raw_ptr(),
                                                 inner.as_ptr().len() - usize(1));
    }

    auto as_ptr() const noexcept [[clang::lifetimebound]] -> char const* {
        return inner.as_ptr().as_raw_ptr();
    }

    auto to_bytes() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        auto pointer = inner.as_ptr();
        return slice<u8>::from_raw_parts(reinterpret_cast<byte const*>(pointer.as_raw_ptr()),
                                         pointer.len() - usize(1));
    }

    auto to_bytes_with_nul() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        auto pointer = inner.as_ptr();
        return slice<u8>::from_raw_parts(reinterpret_cast<byte const*>(pointer.as_raw_ptr()),
                                         pointer.len());
    }

    auto into_bytes() && -> Vec<u8> {
        auto        pointer = inner.as_ptr();
        auto        result  = Vec<u8>::with_capacity(pointer.len() - usize(1));
        auto const* chars   = pointer.as_raw_ptr();
        for (rstd::size_t index = 0; index + 1 < pointer.len().to_primitive(); ++index) {
            result.push(u8(std::bit_cast<rstd::uint8_t>(chars[index])));
        }
        return result;
    }

    auto into_string() && -> Result<String, IntoStringError>;

    auto clone() const -> CString {
        auto pointer = inner.as_ptr();
        auto chars =
            Vec<char>::from(slice<char>::from_raw_parts(pointer.as_raw_ptr(), pointer.len()));
        return CString(chars.into_boxed_slice());
    }
};

export class IntoStringError {
    CString               inner_;
    rstd::str_::Utf8Error error_;

public:
    IntoStringError(CString&& inner, rstd::str_::Utf8Error error)
        : inner_(rstd::move(inner)), error_(error) {}

    auto utf8_error() const noexcept -> rstd::str_::Utf8Error { return error_; }
    auto into_cstring() && -> CString { return rstd::move(inner_); }

private:
    template<typename, typename>
    friend struct rstd::Impl;
};

auto CString::into_string() && -> Result<String, IntoStringError> {
    auto validation = rstd::str_::validate_utf8(to_bytes());
    if (validation.is_err()) {
        return Err(
            IntoStringError(rstd::move(*this), rstd::move(validation).unwrap_err_unchecked()));
    }
    return Ok(String::from_utf8_unchecked(rstd::move(*this).into_bytes()));
}

} // namespace alloc::ffi

using ::alloc::ffi::CString;

namespace rstd
{

template<>
struct Impl<Clone, CString> : DefaultInImpl<Clone, CString> {
    auto clone() const -> CString { return this->self().clone(); }
};

template<mtp::same_as<AsRef<ffi::CStr>> T, mtp::same_as<CString> A>
struct Impl<T, A> : ImplBase<A> {
    auto as_ref() const noexcept -> ref<ffi::CStr> { return this->self().as_ref(); }
};

template<>
struct Impl<fmt::Display, ::alloc::ffi::NulError> : ImplBase<::alloc::ffi::NulError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make(
            "nul byte found in provided data at position: {}", this->self().nul_position()));
    }
};

template<>
struct Impl<fmt::Debug, ::alloc::ffi::NulError> : ImplBase<::alloc::ffi::NulError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(
            fmt::Arguments::make("NulError({})", this->self().nul_position()));
    }
};

template<>
struct Impl<error::Error, ::alloc::ffi::NulError>
    : DefaultInImpl<error::Error, ::alloc::ffi::NulError> {};

template<>
struct Impl<fmt::Display, ::alloc::ffi::FromVecWithNulError>
    : ImplBase<::alloc::ffi::FromVecWithNulError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto error = this->self().error();
        return as<fmt::Display>(error).fmt(formatter);
    }
};

template<>
struct Impl<fmt::Debug, ::alloc::ffi::FromVecWithNulError>
    : ImplBase<::alloc::ffi::FromVecWithNulError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char prefix[] = "FromVecWithNulError { error: ";
        if (! formatter.write_raw(prefix, sizeof(prefix) - 1)) return false;
        auto error = this->self().error();
        if (! as<fmt::Debug>(error).fmt(formatter)) return false;
        return formatter.write_raw(" }", 2);
    }
};

template<>
struct Impl<error::Error, ::alloc::ffi::FromVecWithNulError>
    : DefaultInImpl<error::Error, ::alloc::ffi::FromVecWithNulError> {};

template<>
struct Impl<fmt::Display, ::alloc::ffi::IntoStringError> : ImplBase<::alloc::ffi::IntoStringError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "C string contained non-utf8 bytes";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Debug, ::alloc::ffi::IntoStringError> : ImplBase<::alloc::ffi::IntoStringError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char prefix[] = "IntoStringError { error: ";
        if (! formatter.write_raw(prefix, sizeof(prefix) - 1)) return false;
        if (! as<fmt::Debug>(this->self().error_).fmt(formatter)) return false;
        return formatter.write_raw(" }", 2);
    }
};

template<>
struct Impl<error::Error, ::alloc::ffi::IntoStringError> : ImplBase<::alloc::ffi::IntoStringError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        return Some(dyn<error::Error>::from_ref(this->self().error_));
    }
};

} // namespace rstd

namespace alloc::vec
{

export extern template class Vec<alloc::ffi::CString>;

}
