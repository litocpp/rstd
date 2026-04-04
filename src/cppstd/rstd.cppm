export module rstd.cppstd;
export import rstd;
export import cppstd;

namespace rstd
{

template<>
struct Impl<fmt::Display, cppstd::string> : ImplBase<cppstd::string> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self();
        return f.write_raw((const u8*)s.data(), s.size());
    }
};

template<>
struct Impl<fmt::Display, cppstd::string_view> : ImplBase<cppstd::string_view> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self();
        return f.write_raw((const u8*)s.data(), s.size());
    }
};

template<>
struct Impl<convert::From<string::String>, cppstd::string> : ImplBase<cppstd::string> {
    static auto from(const string::String& s) -> cppstd::string { return { s.begin(), s.end() }; }
};

template<>
struct Impl<convert::From<string::String>, cppstd::string_view> : ImplBase<cppstd::string_view> {
    static auto from(const string::String& s) -> cppstd::string { return { s.begin(), s.end() }; }
};

} // namespace rstd