module;
#include <rstd/macro.hpp>
export module rstd.alloc:vec;
export import :boxed;
export import rstd.core;

using namespace rstd;
using alloc::boxed::Box;

namespace alloc::vec
{
export template<typename T>
class Vec {
    cppstd::vector<T> inner;

    constexpr Vec(cppstd::vector<T> inner): inner(inner) {}
    static_assert(Impled<T, Sized>);

public:
    USE_TRAIT(Vec)

    constexpr Vec() = default;

    constexpr Vec(const Self&)            = delete;
    constexpr Vec& operator=(const Self&) = delete;

    constexpr Vec(Self&&) noexcept            = default;
    constexpr Vec& operator=(Self&&) noexcept = default;

    static constexpr auto make() -> Self { return {}; }
    static constexpr auto with_capacity(usize capacity) -> Self {
        return { cppstd::vector<T>(capacity) };
    }

    constexpr auto as_slice() const noexcept -> slice<T> {
        return slice<T>::from_raw_parts(inner.data(), inner.size());
    }

    constexpr auto as_ptr() const noexcept -> ptr<T> {
        return ptr<T>::from_raw_parts(inner.data());
    }

    auto into_boxed_slice() noexcept -> boxed::Box<T[]> {
        auto raw = new T[inner.size()];
        // TODO: Use memcpy if T is trivially relocatable
        for (usize i = 0; i != inner.size(); ++i) {
            raw[i] = rstd::move(inner[i]);
        }
        auto b = boxed::Box<T[]>::from_raw(mut_ptr<T[]>::from_raw_parts(raw, inner.size()));
        inner.clear();
        return b;
    }

    constexpr void push(T&& value) { inner.emplace_back(rstd::move(value)); }
    constexpr auto pop() -> Option<T> {
        if (inner.empty()) {
            return None();
        } else {
            T value = rstd::move(inner.back());
            inner.pop_back();
            return Some(value);
        }
    }

    constexpr void push_back(const T& value) { inner.push_back(value); }
    constexpr void pop_back() { inner.pop_back(); }

    constexpr T&       at(usize index) { return inner.at(index); }
    constexpr const T& at(usize index) const { return inner.at(index); }

    constexpr T&       operator[](usize index) { return inner[index]; }
    constexpr const T& operator[](usize index) const { return inner[index]; }

    constexpr usize len() const { return inner.size(); }
    constexpr bool  is_empty() const { return inner.empty(); }

    constexpr void clear() { inner.clear(); }

    constexpr T remove(usize index) {
        T value = rstd::move(inner[index]);
        inner.erase(inner.begin() + index);
        return value;
    }

    friend constexpr auto begin(const Self& self) noexcept -> cppstd::vector<T>::const_iterator {
        return cppstd::ranges::begin(self.inner);
    }
    friend constexpr auto end(const Self& self) noexcept -> cppstd::vector<T>::const_iterator {
        return cppstd::ranges::end(self.inner);
    }
    friend constexpr auto size(const Self& self) noexcept -> usize { return self.len(); }
};
} // namespace alloc::vec
using alloc::vec::Vec;
namespace rstd
{
template<typename U, mtp::same_as<cmp::PartialEq<Vec<U>>> T>
struct Impl<T, Vec<U>> : ImplBase<default_tag<Vec<U>>> {
    auto eq(const Vec<U>& other) const noexcept -> bool {
        return this->self().inner == other.inner;
    }
};

template<typename A, mtp::same_as<convert::From<Box<A[]>>> T>
struct Impl<T, Vec<A>> : ImplBase<Vec<A>> {
    static auto from(Box<A[]> b) -> Vec<A> {
        auto ptr = b.as_mut_ptr();
        auto len = ptr.len();
        auto vec = Vec<A>::with_capacity(len);
        // TODO: Use memcpy if T is trivially relocatable
        for (usize i = 0; i != len; ++i) {
            vec.push(rstd::move(ptr[i]));
        }
        return vec;
    }
};

} // namespace rstd