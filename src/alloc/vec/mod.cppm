export module rstd.alloc:vec;
export import rstd.core;

namespace rstd::alloc
{
export template<typename T>
class Vec {
    cppstd::vector<T> inner;

    constexpr Vec() = default;

public:
    using Self = Vec<T>;

    static constexpr auto make() -> Self { return Self {}; }
    static constexpr auto with_capacity(usize capacity) -> Self {
        return { .inner = cppstd::vector<T>(capacity) };
    }

    constexpr void push_back(const T& value) { inner.push_back(value); }
    constexpr void pop_back() { inner.pop_back(); }

    constexpr T&    at(usize index) { return inner.at(index); }
    constexpr usize size() const { return inner.size(); }
};
} // namespace rstd::alloc
