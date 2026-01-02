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
    constexpr ref(char const (&ptr)[N]) noexcept
        : m_ptr(reinterpret_cast<u8 const*>(ptr)), m_length(N) {}
    template<auto N>
    constexpr ref(u8 const (&ptr)[N]) noexcept: m_ptr(ptr), m_length(N) {}

    constexpr auto size() const { return m_length; }
    constexpr auto data() const { return m_ptr; }

    constexpr operator bool() const { return m_length > 0 && m_ptr != nullptr; }
};

export template<>
struct ptr<str_::Str> {
private:
    u8 const* m_ptr;
    usize     m_length;

public:
};

export using str = str_::Str;

static_assert(meta::is_constructible_v<ref<str>, u8 const[3]>);
} // namespace rstd