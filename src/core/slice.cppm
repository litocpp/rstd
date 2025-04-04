module;
#include <memory>
export module rstd.core:slice;

export import :basic;

namespace rstd
{
template<typename T>
class Slice {
    using value_type      = T;
    using size_type       = usize;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;

    constexpr Slice(): m_ptr(nullptr), m_size(0) {}
    constexpr Slice(T* ptr, usize size): m_ptr(ptr), m_size(size) {}
    constexpr Slice(T& ptr, usize size): m_ptr(std::addressof(ptr)), m_size(size) {}

    constexpr auto      data() const -> pointer { return m_ptr; }
    constexpr auto      size() const -> size_type { return m_size; }
    constexpr auto      begin() const -> pointer { return m_ptr; }
    constexpr auto      end() const -> pointer { return m_ptr + m_size; }
    constexpr reference operator[](size_type idx) const { return *(m_ptr + idx); }

private:
    T*    m_ptr;
    usize m_size;
};

} // namespace rstd