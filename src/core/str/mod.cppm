export module rstd.core:str;
export import :core;
export import :meta;

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
export template<>
struct ref<str_::Str> {
private:
    u8 const* m_ptr { nullptr };
    usize     m_length { 0 };

public:
    constexpr ref() noexcept = default;

    template<typename T>
        requires str_::ViewableStr<T>
    constexpr ref(const T& t) noexcept(noexcept(rstd::declval<T>().data()))
        : m_ptr((u8 const*)t.data()), m_length(t.size()) {};

    template<auto N>
    constexpr ref(char const (&ptr)[N]) noexcept: m_ptr((u8 const*)(ptr)), m_length(N) {}
    template<auto N>
    constexpr ref(u8 const (&ptr)[N]) noexcept: m_ptr(ptr), m_length(N) {}
    constexpr ref(u8 const* begin, u8 const* end) noexcept: m_ptr(begin), m_length(end - begin) {}

    constexpr ref(char const* c_str) noexcept
        : m_ptr((u8 const*)(c_str)), m_length(char_traits<char>::length(c_str)) {}

    constexpr auto size() const { return m_length; }
    constexpr auto data() const { return m_ptr; }
    constexpr auto begin() const { return m_ptr; }
    constexpr auto end() const { return m_ptr + m_length; }
    constexpr      operator bool() const { return m_length > 0 && m_ptr != nullptr; }

    constexpr auto operator[](usize i) const -> u8 const& { return *(m_ptr + i); }
};

export using str = str_::Str;

export [[nodiscard]]
constexpr bool operator==(ref<str> a, meta::type_identity_t<ref<str>> b) noexcept {
    return a.size() == b.size() &&
           strncmp((char const*)a.data(), (char* const)b.data(), a.size()) == 0;
}

export template<>
struct ptr<str_::Str> {
private:
    u8 const* m_ptr;
    usize     m_length;

public:
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

static_assert(meta::is_constructible_v<ref<str>, u8 const[3]>);
} // namespace rstd

export template<>
struct rstd::cppstd::formatter<rstd::ref<rstd::str>>
    : rstd::cppstd::formatter<rstd::cppstd::string_view> {
    template<class FmtContext>
    FmtContext::iterator format(rstd::ref<rstd::str> str, FmtContext& ctx) const {
        return rstd::cppstd::formatter<rstd::cppstd::string_view>::format(
            { reinterpret_cast<char const*>(str.data()), str.size() }, ctx);
    }
};
