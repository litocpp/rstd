export module rstd.string;
export import rstd.core;

namespace rstd::string
{

export class String {
    cppstd::vector<u8> vec;

    template<typename, typename>
    friend struct rstd::Impl;

public:
    using value_type                 = u8;
    constexpr String()               = default;
    constexpr String(const String&)  = default;
    String& operator=(const String&) = default;

    String(char const* cstr): vec(cstr, cstr + cppstd::strlen(cstr)) {}

    friend constexpr auto operator<=>(const String& a, const String& b) noexcept {
        return cppstd::lexicographical_compare_three_way(
            a.vec.begin(), a.vec.end(), b.vec.begin(), b.vec.end());
    }
    friend constexpr bool operator==(const String&, const String&) = default;

    void push_back(char c) { vec.push_back(static_cast<u8>(c)); }
    void push_back(u8 c) { vec.push_back(c); }
};

export struct ToString {
    template<typename T, typename = void>
    struct Api {
        auto to_string() const -> String;
    };

    template<typename T>
    using TCollect = TraitCollect<&T::to_string>;
};

} // namespace rstd::string
namespace rstd
{

export using String = string::String;
template<meta::same_as<string::ToString> T, Impled<fmt::Display> A>
struct Impl<T, A> : ImplBase<A> {
    auto to_string() const -> string::String {
        string::String out;
        fmt::format_to(cppstd::back_inserter(out), "{}", this->self());
        return out;
    }
};

export template<Impled<string::ToString> A>
auto to_string(A&& a) {
    // use lvalue
    return as<string::ToString>(a).to_string();
}

namespace fmt
{
export template<typename... Args>
auto format(fmt::format_string<Args...> fmt, Args&&... args) -> string::String {
    return vformat(fmt.get(), fmt::make_format_args(args...));
}

export auto vformat(ref<str> fmt, fmt::format_args args) -> string::String {
    string::String buf;
    fmt::vformat_to(cppstd::back_inserter(buf), { (char const*)fmt.data(), fmt.size() }, args);
    return buf;
}

} // namespace fmt

export using fmt::format;
export using fmt::vformat;

} // namespace rstd
