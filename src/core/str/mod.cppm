export module rstd.core:str;
export import :core;
export import :fmt;

namespace rstd::str_
{
export struct Str;
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
struct ref<str_::Str> : ref_base<ref<str_::Str>, u8[], false> {
public:
    u8 const* p { nullptr };
    usize     length { 0 };

    constexpr ref() noexcept = default;

    template<typename T>
        requires str_::ViewableStr<T>
    constexpr ref(const T& t) noexcept(noexcept(rstd::declval<T>().data()))
        : p((u8 const*)t.data()), length(t.size()) {};

    constexpr ref(slice<u8> p) noexcept: p(p.p), length(p.length) {}

    constexpr ref(char const* c_str) noexcept
        : p(rstd::bit_cast<u8 const*>(c_str)), length(char_traits<char>::length(c_str)) {}

    static constexpr auto from_raw_parts(u8 const* p, usize length) -> ref {
        auto out   = ref();
        out.p      = p;
        out.length = length;
        return out;
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
struct ptr<str_::Str> {
private:
    u8 const* m_ptr;
    usize     m_length;

public:
    using value_type         = u8;
    constexpr ptr() noexcept = default;

    template<typename T>
        requires str_::ViewableStr<T>
    constexpr ptr(const T& t) noexcept(noexcept(rstd::declval<T>().data()))
        : m_ptr((u8 const*)t.data()), m_length(t.size()) {};

    constexpr auto size() const { return m_length; }
    constexpr auto data() const { return m_ptr; }
    constexpr auto begin() const { return m_ptr; }
    constexpr auto end() const { return m_ptr + m_length; }
    constexpr      operator bool() const { return m_length > 0 && m_ptr != nullptr; }

    constexpr auto operator[](usize i) const -> u8 const& { return *(m_ptr + i); }
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
