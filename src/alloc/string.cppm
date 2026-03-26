module;
#include <rstd/macro.hpp>
export module rstd.alloc:string;
export import :vec;
export import rstd.core;

using ::alloc::vec::Vec;

namespace ffi = rstd::ffi;
using namespace rstd::prelude;

namespace alloc::string
{

export class String {
    Vec<u8> vec;

    constexpr String(Vec<u8>&& p): vec(rstd::move(p)) {}

public:
    USE_TRAIT(String)
    constexpr String()                 = default;
    constexpr String(Self&&) noexcept  = default;
    String& operator=(Self&&) noexcept = default;

    using value_type = u8;

    static auto make() -> String { return {}; }
    static auto from_utf8_unchecked(Vec<u8>&& bytes) -> String {
        return String { rstd::move(bytes) };
    }

    auto as_ref() const noexcept -> ref<ffi::CStr> {
        auto p = as_cast<ffi::CStr const*>(&*vec.as_ptr());
        return ref<ffi::CStr>::from_raw_parts(p, vec.len());
    }

    constexpr operator ref<str>() const {
        return ref<str>::from_raw_parts(&*vec.as_ptr(), vec.len());
    }

    void push_back(char c) { vec.push(static_cast<u8>(c)); }
    void push_back(u8 c) { vec.push(rstd::move(c)); }

    friend constexpr auto operator<=>(const String& a, const String& b) noexcept {
        return cppstd::lexicographical_compare_three_way(
            a.vec.begin(), a.vec.end(), b.vec.begin(), b.vec.end());
    }
    friend constexpr auto operator<=>(const String& a, slice<u8> b) noexcept {
        auto ptr = &*b;
        return cppstd::lexicographical_compare_three_way(
            a.vec.begin(), a.vec.end(), ptr, ptr + b.len());
    }
    friend bool operator==(char const* b, const String& a) noexcept {
        return cppstd::lexicographical_compare_three_way(
                   a.vec.begin(), a.vec.end(), b, b + rstd::strlen(b)) ==
               rstd::strong_ordering::equal;
    }

    constexpr auto begin() const noexcept -> const u8* { return vec.begin(); }
    constexpr auto end() const noexcept -> const u8* { return vec.end(); }
    constexpr auto size() const noexcept -> usize { return vec.len(); }
};

export struct ToString {
    template<typename T, typename = void>
    struct Api {
        auto to_string() const -> String;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::to_string>;
};

} // namespace alloc::string

using ::alloc::string::String;
using ::alloc::string::ToString;

namespace rstd
{
template<>
struct Impl<fmt::Write, String> : ImplBase<String> {
    auto write_str(const u8* p, usize len) -> bool {
        auto& self = this->self();
        for (usize i = 0; i < len; ++i) {
            self.push_back(p[i]);
        }
        return true;
    }
};
}

namespace rstd::fmt
{

export template<typename... Args>
auto format(ref<str> fmt_str, Args&&... args) -> String {
    auto buf = String::make();
    Formatter f(buf);
    if constexpr (sizeof...(Args) > 0) {
        Argument arg_array[] = { Argument::display(args)... };
        f.write_fmt({ fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) });
    } else {
        f.write_fmt({ fmt_str.data(), fmt_str.size(), nullptr, 0 });
    }
    return buf;
}

} // namespace rstd::fmt

namespace rstd
{

export using fmt::format;

template<mtp::is_int T>
struct Impl<fmt::Display, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& f) const -> bool {
        char  buf[64];
        char* p   = buf + 64;
        auto  val = this->self();
        if (val == 0) {
            return f.write_raw((const u8*)"0", 1);
        }
        unsigned __int128 uval;
        bool              neg = false;
        if constexpr (mtp::same_as<T, i8> || mtp::same_as<T, i16> || mtp::same_as<T, i32> ||
                      mtp::same_as<T, i64> || mtp::same_as<T, i128> || mtp::same_as<T, int> || mtp::same_as<T, long> || mtp::same_as<T, long long>) {
            if (val < 0) {
                neg  = true;
                uval = (val == numeric_limits<T>::min())
                           ? (unsigned __int128)numeric_limits<T>::max() + 1
                           : (unsigned __int128)-val;
            } else {
                uval = (unsigned __int128)val;
            }
        } else {
            uval = (unsigned __int128)val;
        }

        while (uval > 0) {
            *--p = (char)('0' + (uval % 10));
            uval /= 10;
        }

        if (neg) *--p = '-';
        return f.write_raw((const u8*)p, (buf + 64) - p);
    }
};

template<>
struct Impl<fmt::Display, ref<str>> : ImplBase<ref<str>> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_raw(this->self().data(), this->self().size());
    }
};

template<>
struct Impl<fmt::Display, char const*> : ImplBase<char const*> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self();
        return f.write_raw((const u8*)s, rstd::strlen(s));
    }
};

template<usize N>
struct Impl<fmt::Display, char[N]> : ImplBase<char[N]> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_raw((const u8*)this->self(), rstd::strlen(this->self()));
    }
};

template<usize N>
struct Impl<fmt::Display, char const[N]> : ImplBase<char const[N]> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_raw((const u8*)this->self(), rstd::strlen(this->self()));
    }
};

template<>
struct Impl<fmt::Display, time::Duration> : ImplBase<time::Duration> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto& d = this->self();
        auto s_str = rstd::format("{}", d.as_secs());
        f.write_raw(s_str.begin(), s_str.size());
        f.write_raw((const u8*)"s ", 2);
        auto n_str = rstd::format("{}", d.subsec_nanos());
        f.write_raw(n_str.begin(), n_str.size());
        return f.write_raw((const u8*)"ns", 2);
    }
};

template<>
struct Impl<fmt::Debug, time::Duration> : ImplBase<time::Duration> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return as<fmt::Display>(this->self()).fmt(f);
    }
};

template<mtp::same_as<ToString> T, Impled<fmt::Display> A>
struct Impl<T, A> : ImplBase<A> {
    auto to_string() const -> String {
        return rstd::format("{}", this->self());
    }
};

template<mtp::same_as<cmp::PartialEq<String>> T, mtp::same_as<String> A>
struct Impl<T, A> : ImplBase<default_tag<A>> {
    auto eq(const String& other) const noexcept -> bool {
        return this->self().size() == other.size() &&
               rstd::mem::memcmp(this->self().begin(), other.begin(), this->self().size()) == 0;
    }
};

template<mtp::same_as<cmp::PartialEq<char const*>> T, mtp::same_as<String> A>
struct Impl<T, A> : ImplBase<default_tag<A>> {
    using Rhs = char const*;
    auto eq(const Rhs& other) const noexcept -> bool {
        auto& a = this->self();
        return cppstd::lexicographical_compare_three_way(
                   a.begin(), a.end(), other, other + rstd::strlen(other)) ==
               rstd::strong_ordering::equal;
    }
};

template<mtp::same_as<Into<Vec<u8>>> T, mtp::same_as<String> A>
struct Impl<T, A> : ImplBase<A> {
    auto into() -> Vec<u8> {
        auto& a = this->self();
        return rstd::move(a.vec);
    }
};

export template<Impled<ToString> A>
auto to_string(A&& a) {
    return as<ToString>(a).to_string();
}

} // namespace rstd
