module;
#include <rstd/macro.hpp>
export module rstd.alloc.string;
export import rstd.alloc.vec;
export import rstd.core;

using rstd::alloc::vec::Vec;

namespace rstd::alloc::string
{

export class String {
    Vec<u8> vec;

public:
    USE_TRAIT(String)

    using value_type = u8;

    constexpr static auto make() -> String { return {}; }

    auto as_ref() const noexcept -> ref<const ffi::CStr> {
        auto p = reinterpret_cast<ffi::CStr const*>(&*vec.as_ptr());
        return { .p = p, .length = vec.len() };
    }

    friend constexpr auto operator<=>(const String& a, const String& b) noexcept {
        return cppstd::lexicographical_compare_three_way(cppstd::ranges::begin(a),
                                                         cppstd::ranges::end(a),
                                                         cppstd::ranges::begin(b),
                                                         cppstd::ranges::end(b));
    }
    friend constexpr auto operator<=>(const String& a, slice<u8> b) noexcept {
        auto ptr = &*b;
        return cppstd::lexicographical_compare_three_way(
            cppstd::ranges::begin(a), cppstd::ranges::end(a), ptr, ptr + b.len());
    }

    void push_back(char c) { vec.push_back(static_cast<u8>(c)); }
    void push_back(u8 c) { vec.push_back(c); }

    friend bool operator==(char const* b, const String& a) noexcept {
        return cppstd::lexicographical_compare_three_way(
                   cppstd::ranges::begin(a), cppstd::ranges::end(a), b, b + cppstd::strlen(b)) ==
               cppstd::strong_ordering::equal;
    }

    friend constexpr auto begin(const Self& self) noexcept -> cppstd::vector<u8>::const_iterator {
        return cppstd::ranges::begin(self.vec);
    }
    friend constexpr auto end(const Self& self) noexcept -> cppstd::vector<u8>::const_iterator {
        return cppstd::ranges::end(self.vec);
    }
    friend constexpr auto size(const Self& self) noexcept -> usize { return self.vec.len(); }
};

export struct ToString {
    template<typename T, typename = void>
    struct Api {
        auto to_string() const -> String;
    };

    template<typename T>
    using TCollect = TraitCollect<&T::to_string>;
};

} // namespace rstd::alloc::string

using String   = rstd::alloc::string::String;
using ToString = rstd::alloc::string::ToString;
namespace rstd
{

template<meta::same_as<ToString> T, Impled<fmt::Display> A>
struct Impl<T, A> : ImplBase<A> {
    auto to_string() const -> String {
        auto out = String::make();
        fmt::format_to(cppstd::back_inserter(out), "{}", this->self());
        return out;
    }
};

template<meta::same_as<cmp::PartialEq<String>> T, meta::same_as<String> A>
struct Impl<T, A> : ImplBase<default_tag<A>> {
    auto eq(const String& other) const noexcept -> bool {
        auto& a = this->self();
        auto& b = other;
        return a.vec == b.vec;
    }
};

template<meta::same_as<cmp::PartialEq<char const*>> T, meta::same_as<String> A>
struct Impl<T, A> : ImplBase<default_tag<A>> {
    using Rhs = char const*;
    auto eq(const Rhs& other) const noexcept -> bool {
        auto& a = this->self();
        auto& b = other;
        return cppstd::lexicographical_compare_three_way(
                   a.vec.begin(), a.vec.end(), b, b + cppstd::strlen(b)) ==
               std::strong_ordering::equal;
    }
};

template<meta::same_as<convert::Into<Vec<u8>>> T, meta::same_as<String> A>
struct Impl<T, A> : ImplBase<A> {
    auto into() -> Vec<u8> {
        auto& a = this->self();
        return rstd::move(a.vec);
    }
};

export template<Impled<ToString> A>
auto to_string(A&& a) {
    // use lvalue
    return as<ToString>(a).to_string();
}

namespace fmt
{
export template<typename... Args>
auto format(fmt::format_string<Args...> fmt, Args&&... args) -> String {
    return vformat(fmt.get(), fmt::make_format_args(args...));
}

export auto vformat(ref<str> fmt, fmt::format_args args) -> String {
    auto buf = String::make();
    fmt::vformat_to(cppstd::back_inserter(buf), { (char const*)fmt.data(), fmt.size() }, args);
    return buf;
}

} // namespace fmt

export using fmt::format;
export using fmt::vformat;

} // namespace rstd
