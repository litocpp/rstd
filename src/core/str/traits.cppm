export module rstd.core:str.traits;
import :num.types;
import :error.trait;
export import :str.str;
export import :result;
export import :iter.traits;
export import :iter.adapters;

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

namespace rstd::str_
{

export struct Bytes : DefaultInClass<Bytes, iter::Iterator> {
    using Item                                = u8;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;

    constexpr explicit Bytes(ref<str> value): remaining_(value) {}

    constexpr auto next() -> Option<Item> {
        if (remaining_.is_empty()) return None();
        auto value = remaining_[usize()];
        if (remaining_.len() == usize(1)) {
            remaining_ = ref<str>::from_raw_parts_unchecked(remaining_.data() + 1, usize());
        } else {
            remaining_ = ref<str>::from_raw_parts_unchecked(remaining_.data() + 1,
                                                            remaining_.len() - usize(1));
        }
        return Some(value);
    }

    constexpr auto next_back() -> Option<Item> {
        if (remaining_.is_empty()) return None();
        auto const index = remaining_.len() - usize(1);
        auto       value = remaining_[index];
        remaining_       = ref<str>::from_raw_parts_unchecked(remaining_.data(), index);
        return Some(value);
    }

    constexpr auto size_hint() const -> iter::SizeHint {
        return { remaining_.len(), Some(remaining_.len()) };
    }

    constexpr auto len() const -> usize { return remaining_.len(); }

private:
    ref<str> remaining_;
};

export struct Chars : DefaultInClass<Chars, iter::Iterator> {
    using Item                         = u32;
    static constexpr bool PROVEN_FUSED = true;

    constexpr explicit Chars(ref<str> value): remaining_(value) {}

    constexpr auto next() -> Option<Item> {
        if (remaining_.is_empty()) return None();
        auto [code_point, length] = char_::decode_utf8(remaining_.data(), remaining_.len());
        if (length == remaining_.len()) {
            remaining_ = ref<str>::from_raw_parts_unchecked(
                remaining_.data() + length.to_primitive(), usize());
        } else {
            remaining_ = ref<str>::from_raw_parts_unchecked(
                remaining_.data() + length.to_primitive(), remaining_.len() - length);
        }
        return Some(u32(code_point));
    }

    constexpr auto size_hint() const -> iter::SizeHint {
        auto const bytes = remaining_.len().to_primitive();
        return { usize(bytes / 4 + (bytes % 4 != 0)), Some(remaining_.len()) };
    }

    constexpr auto as_str() const noexcept -> ref<str> { return remaining_; }

private:
    ref<str> remaining_;
};

} // namespace rstd::str_

namespace rstd
{

constexpr auto ref<str>::bytes() const noexcept -> str_::Bytes {
    return str_::Bytes(*this);
}

constexpr auto ref<str>::chars() const noexcept -> str_::Chars {
    return str_::Chars(*this);
}

constexpr auto mut_ref<str>::bytes() const noexcept -> str_::Bytes {
    return as_ref().bytes();
}

constexpr auto mut_ref<str>::chars() const noexcept -> str_::Chars {
    return as_ref().chars();
}

} // namespace rstd

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
    return Ok(str_::from_utf8_unchecked(bytes));
}

} // namespace rstd::str_

namespace rstd
{

constexpr auto ref<str>::find(ref<str> pattern) const noexcept -> Option<usize> {
    if (pattern.is_empty()) {
        usize z;
        return Some(rstd::move(z));
    }
    if (pattern.len() > length) return None();
    auto const limit = (length - pattern.len()).to_primitive();
    for (rstd::size_t index = 0; index <= limit; ++index) {
        auto candidate = ref<str>::from_raw_parts_unchecked(p + index, length - usize(index));
        if (candidate.starts_with(pattern)) {
            usize r(index);
            return Some(rstd::move(r));
        }
    }
    return None();
}

constexpr auto ref<str>::rfind(ref<str> pattern) const noexcept -> Option<usize> {
    if (pattern.is_empty()) return Some(usize(length.to_primitive()));
    if (pattern.len() > length) return None();
    auto offset = (length - pattern.len()).to_primitive();
    for (;;) {
        auto candidate = ref<str>::from_raw_parts_unchecked(p + offset, length - usize(offset));
        if (candidate.starts_with(pattern)) {
            return Some(usize(offset));
        }
        if (offset == 0) break;
        --offset;
    }
    return None();
}

constexpr auto ref<str>::get(usize start, usize end) const noexcept -> Option<ref<str>> {
    if (start > end || end > length) return None();
    if (! is_char_boundary(start) || ! is_char_boundary(end)) return None();
    auto* data = p;
    if (start != usize()) data += start.to_primitive();
    return Some(ref<str>::from_raw_parts_unchecked(data, end - start));
}

constexpr auto ref<str>::strip_prefix(ref<str> pattern) const noexcept -> Option<ref<str>> {
    if (! starts_with(pattern)) return None();
    return get(pattern.len(), length);
}

constexpr auto ref<str>::strip_suffix(ref<str> pattern) const noexcept -> Option<ref<str>> {
    if (! ends_with(pattern)) return None();
    return get(usize(), length - pattern.len());
}

constexpr auto ref<str>::split_once(ref<str> pattern) const noexcept
    -> Option<rstd::tuple<ref<str>, ref<str>>> {
    auto offset = find(pattern);
    if (offset.is_none()) return None();
    auto left  = get(usize(), *offset).unwrap();
    auto right = get(*offset + pattern.len(), length).unwrap();
    return Some(rstd::tuple<ref<str>, ref<str>>(left, right));
}

constexpr auto ref<str>::rsplit_once(ref<str> pattern) const noexcept
    -> Option<rstd::tuple<ref<str>, ref<str>>> {
    auto offset = rfind(pattern);
    if (offset.is_none()) return None();
    auto left  = get(usize(), *offset).unwrap();
    auto right = get(*offset + pattern.len(), length).unwrap();
    return Some(rstd::tuple<ref<str>, ref<str>>(left, right));
}

constexpr auto mut_ref<str>::find(ref<str> pattern) const noexcept -> Option<usize> {
    return as_ref().find(pattern);
}

constexpr auto mut_ref<str>::rfind(ref<str> pattern) const noexcept -> Option<usize> {
    return as_ref().rfind(pattern);
}

constexpr auto mut_ref<str>::get(usize start, usize end) const noexcept -> Option<ref<str>> {
    return as_ref().get(start, end);
}

constexpr auto mut_ref<str>::strip_prefix(ref<str> pattern) const noexcept -> Option<ref<str>> {
    return as_ref().strip_prefix(pattern);
}

constexpr auto mut_ref<str>::strip_suffix(ref<str> pattern) const noexcept -> Option<ref<str>> {
    return as_ref().strip_suffix(pattern);
}

constexpr auto mut_ref<str>::split_once(ref<str> pattern) const noexcept
    -> Option<rstd::tuple<ref<str>, ref<str>>> {
    return as_ref().split_once(pattern);
}

constexpr auto mut_ref<str>::rsplit_once(ref<str> pattern) const noexcept
    -> Option<rstd::tuple<ref<str>, ref<str>>> {
    return as_ref().rsplit_once(pattern);
}

} // namespace rstd

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
