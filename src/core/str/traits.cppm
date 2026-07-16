export module rstd.core:str.traits;
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
    usize offset = 0;
    while (offset < bytes.len()) {
        auto [code_point, consumed] =
            char_::decode_utf8(bytes.as_raw_ptr() + offset, bytes.len() - offset);
        if (code_point == char_::REPLACEMENT && consumed <= 1 && bytes[offset] > 0x7F) {
            return Err(Utf8Error { offset });
        }
        offset += consumed;
    }
    return Ok(empty {});
}

/// Validates a byte slice as UTF-8 and returns a string slice on success.
export constexpr auto from_utf8(slice<u8> bytes) noexcept -> Option<ref<str>> {
    if (validate_utf8(bytes).is_ok()) {
        ref<str> r;
        r.p      = bytes.as_raw_ptr();
        r.length = bytes.len();
        return Some(rstd::move(r));
    }
    return None();
}

/// Finds the byte offset of `needle` in `haystack`.
export constexpr auto find(ref<str> haystack, ref<str> needle) noexcept -> Option<usize> {
    if (needle.size() == 0) {
        usize z = 0;
        return Some(rstd::move(z));
    }
    if (needle.size() > haystack.size()) return None();
    for (usize i = 0; i <= haystack.size() - needle.size(); i++) {
        if (__builtin_memcmp(haystack.data() + i, needle.data(), needle.size()) == 0) {
            usize r = i;
            return Some(rstd::move(r));
        }
    }
    return None();
}

} // namespace rstd::str_
