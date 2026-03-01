export module rstd.core:str;
export import :core;
export import :fmt;
export import :marker;

namespace rstd::str_
{
export struct Str {
    ~Str() = delete;
};

template<typename T>
concept ViewableStr = requires(T t) {
    { t.data() } -> meta::convertible_to<char const*>;
    { t.size() } -> meta::same_as<usize>;
} || requires(T t) {
    { t.data() } -> meta::convertible_to<u8 const*>;
    { t.size() } -> meta::same_as<usize>;
};

} // namespace rstd::str_

namespace rstd
{

template<>
struct Impl<Sized, str_::Str> {
    ~Impl() = delete;
};

template<>
struct Impl<ptr_::Pointee, str_::Str> {
    using Metadata = usize;
};

template<>
struct ref<str_::Str> : ref_base<ref<str_::Str>, u8[], false> {
public:
    u8 const* p { nullptr };
    usize     length { 0 };

    using Self = ref;

    constexpr ref() noexcept = default;

    template<typename T>
        requires str_::ViewableStr<T>
    constexpr ref(const T& t) noexcept(noexcept(rstd::declval<T>().data()))
        : p((u8 const*)t.data()), length(t.size()) {};

    constexpr ref(u8 const* p, usize length) noexcept: p(p), length(length) {}
    constexpr ref(slice<u8> p) noexcept: ref(p.p, p.length) {}

    constexpr ref(char const* c_str) noexcept
        : ref(rstd::bit_cast<u8 const*>(c_str), char_traits<char>::length(c_str)) {}

    static constexpr auto from_raw_parts(value_type* p, usize length) noexcept -> Self {
        return { p, length };
    }

    constexpr auto size() const { return length; }
    constexpr auto data() const { return p; }

    constexpr auto begin() const { return p; }
    constexpr auto end() const { return p + length; }

    constexpr operator bool() const { return length > 0 && p != nullptr; }
};

export using str = str_::Str;

export [[nodiscard]]
constexpr bool operator==(ref<str> a, ref<str> b) noexcept {
    return a.size() == b.size() &&
           strncmp((char const*)a.data(), (char* const)b.data(), a.size()) == 0;
}

template<>
struct ptr<str_::Str> : ptr_base<ptr<str_::Str>, u8[], false> {
public:
    u8 const* p { nullptr };
    usize     length { 0 };

    using value_type         = u8;
    using Self               = ptr;
    constexpr ptr() noexcept = default;

    template<typename T>
        requires str_::ViewableStr<T>
    constexpr ptr(const T& t) noexcept(noexcept(rstd::declval<T>().data()))
        : p((u8 const*)t.data()), length(t.size()) {};

    constexpr ptr(u8 const* p, usize length) noexcept: p(p), length(length) {}

    static constexpr auto from_raw_parts(value_type* p, usize length) noexcept -> Self {
        return { p, length };
    }

    constexpr auto size() const { return length; }
    constexpr auto data() const { return p; }
    constexpr auto begin() const { return p; }
    constexpr auto end() const { return p + length; }

    constexpr operator bool() const { return length > 0 && p != nullptr; }
};

} // namespace rstd

template<>
struct rstd::fmt::formatter<rstd::ref<rstd::str>>
    : rstd::fmt::formatter<rstd::cppstd::string_view> {
    template<class FmtContext>
    FmtContext::iterator format(rstd::ref<rstd::str> str, FmtContext& ctx) const {
        return rstd::fmt::formatter<rstd::cppstd::string_view>::format(
            { reinterpret_cast<char const*>(str.data()), str.size() }, ctx);
    }
};

namespace rstd::str_
{
export constexpr auto extract_last(ref<str> path, usize count) -> ref<str> {
    auto size = path.size();
    while (size != 0) {
        if (path[size - 1] == '/' || path[size - 1] == '\\') {
            --count;
        }
        if (count != 0) {
            --size;
        } else {
            break;
        }
    }
    return ref<str>::from_raw_parts(path.begin() + size, path.end() - path.begin());
}
} // namespace rstd::str_