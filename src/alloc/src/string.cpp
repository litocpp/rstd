module;
#include <rstd/macro.hpp>

module rstd.alloc;
import :string;

using ::alloc::string::FromUtf8Error;
using ::alloc::string::String;
using ::alloc::vec::Vec;
using namespace rstd::prelude;

namespace alloc::string
{

auto String::with_capacity(usize capacity) -> String {
    auto result = try_with_capacity(capacity);
    if (result.is_err()) rstd::panic { "String allocation failed" };
    return result.unwrap_unchecked();
}

auto String::try_with_capacity(usize capacity) -> Result<String, collections::TryReserveError> {
    auto buffer = Vec<u8>::try_with_capacity(capacity);
    if (buffer.is_err()) return Err(buffer.unwrap_err_unchecked());
    return Ok(String(buffer.unwrap_unchecked()));
}

auto String::make(ref<str> value) -> String {
    return String { Vec<u8>::from(value.as_bytes()) };
}

auto String::clone() const -> String {
    return String::make(as_str());
}

void String::clone_from(const String& source) {
    *this = source.clone();
}

auto String::from_utf8_unchecked(Vec<u8>&& bytes) -> String {
    return String { rstd::move(bytes) };
}

auto String::from_utf8(Vec<u8>&& bytes) -> Result<String, FromUtf8Error> {
    auto validation = rstd::str_::validate_utf8(bytes.as_slice());
    if (validation.is_err()) {
        return Err(FromUtf8Error(rstd::move(bytes), rstd::move(validation).unwrap_err_unchecked()));
    }
    return Ok(String { rstd::move(bytes) });
}

String::operator ref<str>() const {
    return as_str();
}

void String::push_str(ref<str> value) {
    if (value.is_empty()) return;
    vec.extend_from_slice(value.as_bytes());
}

void String::push_ascii(u8 value) {
    if (! value.is_ascii()) rstd::panic("String::push_ascii requires ASCII");
    vec.push(rstd::move(value));
}

void String::push_ascii(char value) {
    push_ascii(u8(value));
}

void String::push(char32_t code_point) {
    auto value = rstd::Char::from_char32(code_point);
    rstd_assert(value.is_some());
    push(value.unwrap_unchecked());
}

void String::push(rstd::Char value) {
    byte bytes[4] {};
    auto length = value.encode_utf8(bytes);
    [[unsafe]] { vec.extend_from_slice(slice<u8>::from_raw_parts(bytes, length)); }
}

auto String::as_str() const noexcept -> ref<str> {
    [[unsafe]] { return rstd::str_::from_utf8_unchecked(vec.as_slice()); }
}

void String::reserve(usize additional) {
    vec.reserve(additional);
}

void String::reserve_exact(usize additional) {
    vec.reserve_exact(additional);
}

auto String::try_reserve(usize additional) -> Result<empty, collections::TryReserveError> {
    return vec.try_reserve(additional);
}

auto String::try_reserve_exact(usize additional) -> Result<empty, collections::TryReserveError> {
    return vec.try_reserve_exact(additional);
}

void String::truncate(usize new_len) {
    if (new_len >= vec.len()) return;
    rstd_assert(as_str().is_char_boundary(new_len));
    while (vec.len() > new_len) vec.pop();
}

void String::replace_range(usize start, usize end, ref<str> replacement) {
    rstd_assert(start <= end && end <= vec.len());
    auto current = as_str();
    rstd_assert(current.is_char_boundary(start));
    rstd_assert(current.is_char_boundary(end));

    auto result = Vec<u8>::with_capacity(start + replacement.size() + vec.len() - end);
    if (start != usize()) {
        [[unsafe]] { result.extend_from_slice(slice<u8>::from_raw_parts(current.data(), start)); }
    }
    result.extend_from_slice(replacement.as_bytes());
    if (end != vec.len()) {
        [[unsafe]] {
            result.extend_from_slice(
                slice<u8>::from_raw_parts(current.data() + end.to_primitive(), vec.len() - end));
        }
    }
    vec = rstd::move(result);
}

void String::insert_str(usize index, ref<str> value) {
    replace_range(index, index, value);
}

void String::insert(usize index, char32_t code_point) {
    auto value = rstd::Char::from_char32(code_point);
    rstd_assert(value.is_some());
    insert(index, value.unwrap_unchecked());
}

void String::insert(usize index, rstd::Char value) {
    byte bytes[4] {};
    auto length = value.encode_utf8(bytes);
    [[unsafe]] {
        insert_str(index,
                   rstd::str_::from_utf8_unchecked(slice<u8>::from_raw_parts(bytes, length)));
    }
}

auto String::begin() const noexcept -> ptr<u8> {
    return vec.as_ptr();
}

auto String::end() const noexcept -> ptr<u8> {
    [[unsafe]] { return vec.as_ptr().add(vec.len()); }
}

auto String::data() const noexcept -> const byte* {
    return vec.as_ptr().as_raw_ptr();
}

auto String::into_bytes() && -> Vec<u8> {
    return rstd::move(vec);
}

} // namespace alloc::string

namespace rstd
{

auto Impl<ops::Deref, String>::deref() const noexcept -> ref<Target> {
    return this->self().as_str();
}

auto Impl<ops::DerefMut, String>::deref_mut() noexcept -> mut_ref<ops::deref_target_t<String>> {
    return this->self().as_mut_str();
}

auto Impl<borrow::Borrow<str>, String>::borrow() const noexcept -> ref<str> {
    return this->self().as_str();
}

auto Impl<fmt::Write, String>::write_str(ref<str> const& value) -> bool {
    this->self().push_str(value);
    return true;
}

auto Impl<fmt::Display, String>::fmt(fmt::Formatter& formatter) const -> bool {
    return formatter.pad(this->self().as_str());
}

auto Impl<fmt::Debug, String>::fmt(fmt::Formatter& formatter) const -> bool {
    auto value = this->self().as_str();
    return as<fmt::Debug>(value).fmt(formatter);
}

auto Impl<fmt::Display, FromUtf8Error>::fmt(fmt::Formatter& formatter) const -> bool {
    auto error = this->self().utf8_error();
    return as<fmt::Display>(error).fmt(formatter);
}

auto Impl<fmt::Debug, FromUtf8Error>::fmt(fmt::Formatter& formatter) const -> bool {
    constexpr char prefix[] = "FromUtf8Error { error: ";
    if (! formatter.write_raw(prefix, sizeof(prefix) - 1)) return false;
    auto error = this->self().utf8_error();
    if (! as<fmt::Debug>(error).fmt(formatter)) return false;
    return formatter.write_raw(" }", 2);
}

auto Impl<fmt::Display, char const*>::fmt(fmt::Formatter& formatter) const -> bool {
    auto value = this->self();
    [[unsafe]] {
        return formatter.write_raw(reinterpret_cast<const uint8_t*>(value), rstd::strlen(value));
    }
}

auto Impl<fmt::Debug, char const*>::fmt(fmt::Formatter& formatter) const -> bool {
    constexpr uint8_t QUOTE[] = { '"' };
    formatter.write_raw(QUOTE, sizeof(QUOTE));
    as<fmt::Display>(this->self()).fmt(formatter);
    return formatter.write_raw(QUOTE, sizeof(QUOTE));
}

auto Impl<fmt::Debug, time::Duration>::fmt(fmt::Formatter& formatter) const -> bool {
    auto write_ascii = [&formatter](auto const* value, size_t length) {
        return formatter.write_raw(value, length);
    };
    auto write_fraction = [&write_ascii]<size_t Size>(char (&fraction)[Size], uint32_t remaining) {
        size_t length = Size - 1;
        [[unsafe]] {
            for (auto index = length; index > 0; --index) {
                fraction[index - 1] = char('0' + remaining % 10);
                remaining /= 10;
            }
            fraction[length] = '\0';
            while (length > 1 && fraction[length - 1] == '0') --length;
        }
        write_ascii(".", 1);
        write_ascii(fraction, length);
    };
    auto&          duration = this->self();
    const uint64_t secs     = duration.as_secs().to_primitive();
    const uint32_t nanos    = duration.subsec_nanos().to_primitive();
    if (secs > 0) {
        auto value = rstd::format("{}", secs);
        write_ascii(value.data(), value.size().to_primitive());
        if (nanos != 0) {
            char fraction[10];
            write_fraction(fraction, nanos);
        }
        return write_ascii("s", 1);
    }
    if (nanos >= time::NANOS_PER_MILLI.to_primitive()) {
        uint32_t milliseconds = nanos / time::NANOS_PER_MILLI.to_primitive();
        uint32_t remaining    = nanos % time::NANOS_PER_MILLI.to_primitive();
        auto     value        = rstd::format("{}", milliseconds);
        write_ascii(value.data(), value.size().to_primitive());
        if (remaining != 0) {
            char fraction[7];
            write_fraction(fraction, remaining);
        }
        return write_ascii("ms", 2);
    }
    if (nanos >= time::NANOS_PER_MICRO.to_primitive()) {
        uint32_t microseconds = nanos / time::NANOS_PER_MICRO.to_primitive();
        uint32_t remaining    = nanos % time::NANOS_PER_MICRO.to_primitive();
        auto     value        = rstd::format("{}", microseconds);
        write_ascii(value.data(), value.size().to_primitive());
        if (remaining != 0) {
            char fraction[4];
            write_fraction(fraction, remaining);
        }
        return write_ascii("us", 2);
    }
    auto value = rstd::format("{}", nanos);
    write_ascii(value.data(), value.size().to_primitive());
    return write_ascii("ns", 2);
}

} // namespace rstd
