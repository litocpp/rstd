export module rstd.cppstd;
export import rstd;
export import cppstd;

export namespace rstd::cppstd
{

inline constexpr int IO_ERROR          = ::cppstd::IO_ERROR;
inline constexpr int SEEK_FROM_START   = ::cppstd::SEEK_FROM_START;
inline constexpr int SEEK_FROM_CURRENT = ::cppstd::SEEK_FROM_CURRENT;
inline constexpr int SEEK_FROM_END     = ::cppstd::SEEK_FROM_END;

inline auto as_str(std::string_view value) noexcept -> ref<str> {
    return ref<str>::from_raw_parts(value.data(), usize(value.size()));
}

inline auto to_string(ref<str> value) -> std::string {
    auto result = std::string {};
    result.reserve(value.size().to_primitive());
    auto bytes = str_::as_bytes(value);
    for (rstd::size_t index = 0; index != bytes.len().to_primitive(); ++index) {
        result.push_back(static_cast<char>(bytes[usize(index)]));
    }
    return result;
}

inline auto as_string_view(ref<str> value) noexcept -> std::string_view {
    return { reinterpret_cast<const char*>(value.data()), value.size().to_primitive() };
}

inline auto to_string(const string::String& value) -> std::string {
    return to_string(value.as_str());
}

} // namespace rstd::cppstd

namespace rstd
{

template<>
struct Impl<fmt::Display, std::string> : ImplBase<std::string> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self();
        return f.write_raw(s.data(), s.size());
    }
};

template<>
struct Impl<fmt::Display, std::string_view> : ImplBase<std::string_view> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self();
        return f.write_raw(s.data(), s.size());
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
