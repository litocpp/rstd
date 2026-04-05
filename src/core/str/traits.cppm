export module rstd.core:str.traits;
export import :str.str;
export import :result;

namespace rstd::str_
{
/// Trait for parsing a value from a string slice.
export struct FromStr {
    template<typename Self, typename = void>
    struct Api {
        using Err = typename Impl<FromStr, Self>::Err;
        static auto from_str(ref<str> str) -> Result<Self, Err> {
            return rstd::trait_static_call<FromStr, Self>(str);
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

/// Validates a byte slice as UTF-8 and returns a string slice on success.
export constexpr auto from_utf8(slice<u8> bytes) noexcept -> Option<ref<str>> {
    if (char_::is_valid_utf8(&*bytes, bytes.len())) {
        ref<str> r;
        r.p = &*bytes;
        r.length = bytes.len();
        return Some(rstd::move(r));
    }
    return None();
}

/// Finds the byte offset of `needle` in `haystack`.
export constexpr auto find(ref<str> haystack, ref<str> needle) noexcept -> Option<usize> {
    if (needle.size() == 0) { usize z = 0; return Some(rstd::move(z)); }
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