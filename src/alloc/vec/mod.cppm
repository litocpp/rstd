module;
#include <rstd/macro.hpp>
export module rstd.alloc.vec;
export import rstd.alloc.boxed;
export import rstd.core;

namespace rstd::alloc::vec
{
export template<typename T>
class Vec {
    cppstd::vector<T> inner;

    static_assert(Impled<T, Sized>);

public:
    USE_TRAIT(Vec)

    constexpr Vec() = default;

    constexpr Vec(const Self&)            = delete;
    constexpr Vec& operator=(const Self&) = delete;

    constexpr Vec(Self&&) noexcept {}
    constexpr Vec& operator=(Self&&) noexcept {}

    static constexpr auto make() -> Self { return {}; }
    static constexpr auto with_capacity(usize capacity) -> Self {
        return { .inner = cppstd::vector<T>(capacity) };
    }

    constexpr auto as_slice() const noexcept -> slice<const T> {
        return slice<const T>::from_raw(*inner.data(), inner.size());
    }

    constexpr auto as_ptr() const noexcept -> ptr<const T> {
        return ptr<const T>::from_raw(inner.data());
    }

    auto into_boxed_slice() noexcept -> boxed::Box<T[]> {
        auto raw = new T[inner.size()];
        // TODO: Use memcpy if T is trivially relocatable
        for (usize i = 0; i != inner.size(); ++i) {
            raw[i] = rstd::move(inner[i]);
        }
        auto b = boxed::Box<T[]>::from_raw(ptr<T[]>::from_raw(raw, inner.size()));
        inner.clear();
        return b;
    }

    constexpr void push(T&& value) { inner.emplace_back(rstd::move(value)); }

    constexpr void push_back(const T& value) { inner.push_back(value); }
    constexpr void pop_back() { inner.pop_back(); }

    constexpr T&       at(usize index) { return inner.at(index); }
    constexpr const T& at(usize index) const { return inner.at(index); }

    constexpr usize len() const { return inner.size(); }

    friend constexpr auto begin(const Self& self) noexcept -> cppstd::vector<T>::const_iterator {
        return cppstd::ranges::begin(self.inner);
    }
    friend constexpr auto end(const Self& self) noexcept -> cppstd::vector<T>::const_iterator {
        return cppstd::ranges::end(self.inner);
    }
    friend constexpr auto size(const Self& self) noexcept -> usize { return self.len(); }
};
} // namespace rstd::alloc::vec
using rstd::alloc::vec::Vec;
namespace rstd
{
template<typename U, meta::same_as<cmp::PartialEq<Vec<U>>> T>
struct Impl<T, Vec<U>> : ImplBase<default_tag<Vec<U>>> {
    auto eq(const Vec<U>& other) const noexcept -> bool {
        return this->self().inner == other.inner;
    }
};

} // namespace rstd