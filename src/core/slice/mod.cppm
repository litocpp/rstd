module;
#include <memory>
#include <format>
#include <cstring>
export module rstd.core:slice;

export import :core;
export import :meta;

namespace rstd
{
template<typename T>
class Slice {
public:
    using value_type      = T;
    using size_type       = usize;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;

    constexpr Slice(): m_ptr(nullptr), m_size(0) {}
    constexpr Slice(T* ptr, usize size): m_ptr(ptr), m_size(size) {}
    constexpr Slice(T& ptr, usize size): m_ptr(std::addressof(ptr)), m_size(size) {}

    template<usize N>
    constexpr Slice(T (&arr)[N]): m_ptr(arr), m_size(N) {}

    template<typename U>
        requires requires(U& r) {
            { r.data() } -> meta::same_as<T>;
            r.size();
        }
    constexpr Slice(U& range): m_ptr(range.data()), m_size(range.size()) {}

    constexpr auto      data() const -> pointer { return m_ptr; }
    constexpr auto      size() const -> size_type { return m_size; }
    constexpr auto      begin() const -> pointer { return m_ptr; }
    constexpr auto      end() const -> pointer { return m_ptr + m_size; }
    constexpr reference operator[](size_type idx) const { return *(m_ptr + idx); }

private:
    T*    m_ptr;
    usize m_size;
};

template<typename T>
    requires meta::same_as<meta::remove_cv_t<T>, char>
class Slice<T> {
public:
    using value_type      = T;
    using size_type       = usize;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;

    constexpr Slice(): m_ptr(nullptr), m_size(0) {}
    constexpr Slice(T* ptr, usize size): m_ptr(ptr), m_size(size) {}
    constexpr Slice(T& ptr, usize size): m_ptr(std::addressof(ptr)), m_size(size) {}

    template<usize N>
    constexpr Slice(T (&arr)[N]): m_ptr(arr), m_size(arr[N - 1] == '\0' ? N - 1 : N) {}

    template<typename U>
        requires requires(U& r) {
            { r.data() } -> meta::convertible_to<pointer>;
            r.size();
        }
    constexpr Slice(U&& range): m_ptr(range.data()), m_size(range.size()) {}

    constexpr auto      data() const -> pointer { return m_ptr; }
    constexpr auto      size() const -> size_type { return m_size; }
    constexpr auto      begin() const -> pointer { return m_ptr; }
    constexpr auto      end() const -> pointer { return m_ptr + m_size; }
    constexpr reference operator[](size_type idx) const { return *(m_ptr + idx); }

private:
    T*    m_ptr;
    usize m_size;
};

export using ref_str = Slice<const char>;
} // namespace rstd

export template<>
struct std::formatter<rstd::ref_str> : std::formatter<std::string_view> {
    template<class FmtContext>
    FmtContext::iterator format(rstd::ref_str str, FmtContext& ctx) const {
        return std::formatter<std::string_view>::format({ str.data(), str.size() }, ctx);
    }
};
