export module rstd.cppstd;
export import rstd;
export import cppstd;

namespace rstd
{

template<>
struct Impl<fmt::Display, std::string> : ImplBase<std::string> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self();
        return f.write_raw((const u8*)s.data(), s.size());
    }
};

template<>
struct Impl<fmt::Display, std::string_view> : ImplBase<std::string_view> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self();
        return f.write_raw((const u8*)s.data(), s.size());
    }
};

template<>
struct Impl<convert::From<string::String>, std::string> : ImplBase<std::string> {
    static auto from(const string::String& s) -> std::string { return { s.begin(), s.end() }; }
};

template<>
struct Impl<convert::From<string::String>, std::string_view> : ImplBase<std::string_view> {
    static auto from(const string::String& s) -> std::string { return { s.begin(), s.end() }; }
};

} // namespace rstd
