export module rstd.alloc:vec;
export import :boxed;
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

    auto as_slice() const noexcept -> slice<T> { return slice<T> { inner.data(), inner.size() }; }
    auto into_boxed_slice() noexcept -> boxed::Box<T[]> {
        auto raw = new T[inner.size()];
        for (usize i = 0; i != inner.size(); ++i) {
            raw[i] = rstd::move(inner[i]);
        }
        auto b = boxed::Box<T[]>::from_raw({ raw, inner.size() });
        inner.clear();
        return b;
    }

    constexpr void push_back(const T& value) { inner.push_back(value); }
    constexpr void pop_back() { inner.pop_back(); }

    constexpr T&    at(usize index) { return inner.at(index); }
    constexpr usize size() const { return inner.size(); }
    constexpr usize len() const { return inner.size(); }
};
} // namespace rstd::alloc
