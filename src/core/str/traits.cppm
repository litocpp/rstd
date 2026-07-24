export module rstd.core:str.traits;
import :num.types;
import :error.trait;
export import :str.str;
export import :result;

namespace rstd::str_
{
/// Trait for parsing a value from a string slice.
export struct FromStr {
    template<typename Self, typename = void>
    struct Api {
        using Trait = FromStr;
        using Err   = typename Impl<FromStr, Self>::Err;
        static auto from_str(ref<str> str) -> Result<Self, Err> {
            return rstd::trait_static_call<0, Api>(str);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::from_str>;
};
} // namespace rstd::str_

namespace rstd
{
/// Parses a string slice into the specified type.
/// \tparam T The type to parse into; must implement `FromStr`.
/// \param str The string slice to parse.
/// \return A `Result` containing the parsed value or an error.
export template<typename T>
auto from_str(ref<str> str) {
    return Impl<str_::FromStr, mtp::rm_cvf<T>>::from_str(str);
}

} // namespace rstd

// ── str functions that return Option ─────────────────────────────────────
namespace rstd::str_
{

export class Utf8Error {
public:
    constexpr Utf8Error(usize valid_up_to, Option<u8> error_len) noexcept
        : valid_up_to_(valid_up_to), error_len_(rstd::move(error_len)) {}

    [[nodiscard]]
    constexpr auto valid_up_to() const noexcept -> usize {
        return valid_up_to_;
    }

    [[nodiscard]]
    constexpr auto error_len() const noexcept -> Option<u8> {
        return error_len_;
    }

private:
    usize      valid_up_to_;
    Option<u8> error_len_;
};

/// Validates a byte slice as UTF-8 and returns the first invalid byte offset on failure.
export constexpr auto validate_utf8(slice<u8> bytes) noexcept -> Result<empty, Utf8Error> {
    auto const length = bytes.len().to_primitive();
    auto       value  = [bytes](rstd::size_t index) constexpr {
        return bytes[usize(index)].to_primitive();
    };
    auto invalid = [](rstd::size_t offset, rstd::uint8_t error_len) constexpr {
        return Result<empty, Utf8Error>(Err(Utf8Error(usize(offset), Some(u8(error_len)))));
    };
    auto incomplete = [](rstd::size_t offset) constexpr {
        return Result<empty, Utf8Error>(Err(Utf8Error(usize(offset), None())));
    };
    auto continuation = [](rstd::uint8_t byte) constexpr {
        return (byte & 0xc0u) == 0x80u;
    };

    rstd::size_t offset = 0;
    while (offset < length) {
        auto const first = value(offset);
        if (first <= 0x7f) {
            ++offset;
            continue;
        }

        if (first >= 0xc2 && first <= 0xdf) {
            if (offset + 1 >= length) return incomplete(offset);
            if (! continuation(value(offset + 1))) return invalid(offset, 1);
            offset += 2;
            continue;
        }

        if (first >= 0xe0 && first <= 0xef) {
            if (offset + 1 >= length) return incomplete(offset);
            auto const second       = value(offset + 1);
            auto const valid_second = first == 0xe0   ? second >= 0xa0 && second <= 0xbf
                                      : first == 0xed ? second >= 0x80 && second <= 0x9f
                                                      : continuation(second);
            if (! valid_second) return invalid(offset, 1);
            if (offset + 2 >= length) return incomplete(offset);
            if (! continuation(value(offset + 2))) return invalid(offset, 2);
            offset += 3;
            continue;
        }

        if (first >= 0xf0 && first <= 0xf4) {
            if (offset + 1 >= length) return incomplete(offset);
            auto const second       = value(offset + 1);
            auto const valid_second = first == 0xf0   ? second >= 0x90 && second <= 0xbf
                                      : first == 0xf4 ? second >= 0x80 && second <= 0x8f
                                                      : continuation(second);
            if (! valid_second) return invalid(offset, 1);
            if (offset + 2 >= length) return incomplete(offset);
            if (! continuation(value(offset + 2))) return invalid(offset, 2);
            if (offset + 3 >= length) return incomplete(offset);
            if (! continuation(value(offset + 3))) return invalid(offset, 3);
            offset += 4;
            continue;
        }

        return invalid(offset, 1);
    }
    return Ok(empty {});
}

/// Validates a byte slice as UTF-8 and returns a string slice on success.
export constexpr auto from_utf8(slice<u8> bytes) noexcept -> Result<ref<str>, Utf8Error> {
    auto validation = validate_utf8(bytes);
    if (validation.is_err()) return Err(rstd::move(validation).unwrap_err_unchecked());
    return Ok(rstd::from_utf8_unchecked(bytes));
}

/// Finds the byte offset of `needle` in `haystack`.
export constexpr auto find(ref<str> haystack, ref<str> needle) noexcept -> Option<usize> {
    if (needle.size() == usize()) {
        usize z;
        return Some(rstd::move(z));
    }
    if (needle.size() > haystack.size()) return None();
    auto const limit = (haystack.size() - needle.size()).to_primitive();
    for (rstd::size_t i = 0; i <= limit; ++i) {
        if (__builtin_memcmp(haystack.data() + i, needle.data(), needle.size().to_primitive()) ==
            0) {
            usize r(i);
            return Some(rstd::move(r));
        }
    }
    return None();
}

/// Returns the last byte offset of `needle` in `haystack`.
export constexpr auto rfind(ref<str> haystack, ref<str> needle) noexcept -> Option<usize> {
    if (needle.size() == usize()) return Some(haystack.size());
    if (needle.size() > haystack.size()) return None();
    auto offset = (haystack.size() - needle.size()).to_primitive();
    for (;;) {
        if (__builtin_memcmp(
                haystack.data() + offset, needle.data(), needle.size().to_primitive()) == 0) {
            return Some(usize(offset));
        }
        if (offset == 0) break;
        --offset;
    }
    return None();
}

/// Returns a checked UTF-8 string slice over `[start, end)` byte offsets.
export constexpr auto get(ref<str> value [[clang::lifetimebound]], usize start, usize end) noexcept
    -> Option<ref<str>> {
    if (start > end || end > value.size()) return None();
    if (! is_char_boundary(value, start) || ! is_char_boundary(value, end)) return None();
    auto* data = value.data();
    if (start != usize()) data += start.to_primitive();
    return Some(ref<str>::from_raw_parts_unchecked(data, end - start));
}

/// Returns the suffix beginning at a checked UTF-8 byte offset.
export constexpr auto get_from(ref<str> value [[clang::lifetimebound]], usize start) noexcept
    -> Option<ref<str>> {
    return get(value, start, value.size());
}

/// Returns the prefix ending at a checked UTF-8 byte offset.
export constexpr auto get_to(ref<str> value [[clang::lifetimebound]], usize end) noexcept
    -> Option<ref<str>> {
    return get(value, usize(), end);
}

/// Removes `prefix` and returns the remaining suffix when it matches.
export constexpr auto strip_prefix(ref<str> value [[clang::lifetimebound]],
                                   ref<str> prefix) noexcept -> Option<ref<str>> {
    if (! starts_with(value, prefix)) return None();
    return get_from(value, prefix.size());
}

/// Removes `suffix` and returns the remaining prefix when it matches.
export constexpr auto strip_suffix(ref<str> value [[clang::lifetimebound]],
                                   ref<str> suffix) noexcept -> Option<ref<str>> {
    if (! ends_with(value, suffix)) return None();
    return get_to(value, value.size() - suffix.size());
}

/// Splits once at the first occurrence of `separator`.
export constexpr auto split_once(ref<str> value [[clang::lifetimebound]],
                                 ref<str> separator) noexcept
    -> Option<rstd::tuple<ref<str>, ref<str>>> {
    auto offset = find(value, separator);
    if (offset.is_none()) return None();
    auto left  = get_to(value, *offset).unwrap();
    auto right = get_from(value, *offset + separator.size()).unwrap();
    return Some(rstd::tuple<ref<str>, ref<str>>(left, right));
}

/// Splits once at the last occurrence of `separator`.
export constexpr auto rsplit_once(ref<str> value [[clang::lifetimebound]],
                                  ref<str> separator) noexcept
    -> Option<rstd::tuple<ref<str>, ref<str>>> {
    auto offset = rfind(value, separator);
    if (offset.is_none()) return None();
    auto left  = get_to(value, *offset).unwrap();
    auto right = get_from(value, *offset + separator.size()).unwrap();
    return Some(rstd::tuple<ref<str>, ref<str>>(left, right));
}

} // namespace rstd::str_

namespace rstd
{

namespace str_error_detail
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
} // namespace str_error_detail

template<>
struct Impl<fmt::Display, str_::Utf8Error> : ImplBase<str_::Utf8Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto length = this->self().error_len();
        if (length.is_some()) {
            constexpr char prefix[] = "invalid utf-8 sequence of ";
            if (! formatter.write_raw(prefix, sizeof(prefix) - 1)) return false;
            if (! str_error_detail::write_usize(formatter, usize((*length).to_primitive())))
                return false;
            constexpr char suffix[] = " bytes from index ";
            if (! formatter.write_raw(suffix, sizeof(suffix) - 1)) return false;
            return str_error_detail::write_usize(formatter, this->self().valid_up_to());
        }
        constexpr char prefix[] = "incomplete utf-8 byte sequence from index ";
        if (! formatter.write_raw(prefix, sizeof(prefix) - 1)) return false;
        return str_error_detail::write_usize(formatter, this->self().valid_up_to());
    }
};

template<>
struct Impl<fmt::Debug, str_::Utf8Error> : ImplBase<str_::Utf8Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char prefix[] = "Utf8Error { valid_up_to: ";
        if (! formatter.write_raw(prefix, sizeof(prefix) - 1)) return false;
        if (! str_error_detail::write_usize(formatter, this->self().valid_up_to())) return false;
        constexpr char separator[] = ", error_len: ";
        if (! formatter.write_raw(separator, sizeof(separator) - 1)) return false;
        auto length = this->self().error_len();
        if (length.is_some()) {
            if (! formatter.write_raw("Some(", 5)) return false;
            if (! str_error_detail::write_usize(formatter, usize((*length).to_primitive())))
                return false;
            if (! formatter.write_raw(")", 1)) return false;
        } else if (! formatter.write_raw("None", 4)) {
            return false;
        }
        return formatter.write_raw(" }", 2);
    }
};

template<>
struct Impl<error::Error, str_::Utf8Error> : ImplBase<str_::Utf8Error> {
    auto source() const noexcept -> Option<error::ErrorRef> { return None(); }
};

} // namespace rstd
