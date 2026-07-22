export module rstd.core:str.traits;
import :num.types;
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
    constexpr explicit Utf8Error(usize valid_up_to) noexcept: valid_up_to_(valid_up_to) {}

    [[nodiscard]]
    constexpr auto valid_up_to() const noexcept -> usize {
        return valid_up_to_;
    }

private:
    usize valid_up_to_;
};

/// Validates a byte slice as UTF-8 and returns the first invalid byte offset on failure.
export constexpr auto validate_utf8(slice<u8> bytes) noexcept -> Result<empty, Utf8Error> {
    rstd::size_t offset = 0;
    while (offset < bytes.len().to_primitive()) {
        auto [code_point, consumed] = char_::decode_utf8(
            bytes.as_raw_ptr() + offset, usize(bytes.len().to_primitive() - offset));
        if (code_point == char_::REPLACEMENT && consumed <= usize(1) &&
            bytes[usize(offset)].to_primitive() > 0x7F) {
            return Err(Utf8Error { usize(offset) });
        }
        offset += consumed.to_primitive();
    }
    return Ok(empty {});
}

/// Validates a byte slice as UTF-8 and returns a string slice on success.
export constexpr auto from_utf8(slice<u8> bytes) noexcept -> Option<ref<str>> {
    if (validate_utf8(bytes).is_ok()) {
        auto     raw = as_bytes(bytes);
        ref<str> r;
        r.p      = raw.as_raw_ptr();
        r.length = raw.len();
        return Some(rstd::move(r));
    }
    return None();
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
    return Some(ref<str>::from_raw_parts(data, end - start));
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
