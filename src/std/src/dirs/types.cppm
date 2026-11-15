module;
#include <rstd/enum.hpp>
export module rstd:dirs.types;
export import :path;
export import rstd.io.error;

using rstd::path::Path;
using rstd::path::PathBuf;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd::dirs
{
class Error {
    RSTD_ENUM(Error,
              (InvalidAppId, (String value;)),
              (InvalidPath, (PathBuf path;)),
              (Unsupported),
              (System, (io::Error cause;)),
              (Platform, (String message; i64 code;)))

public:
    static auto system(io::Error cause) -> Error { return System(rstd::move(cause)); }
    static auto platform(String message, i64 code) -> Error {
        return Platform(rstd::move(message), code);
    }
    static auto unsupported() -> Error { return Unsupported(); }
    auto        kind() const noexcept -> Tag { return tag(); }
};

class AppId {
    String value_;
    explicit AppId(String value): value_(rstd::move(value)) {}

public:
    static auto parse(ref<str> value) -> Result<AppId, Error> {
        if (value.is_empty() || value == "."_str || value == ".."_str ||
            value.as_bytes()[value.len() - usize(1)] == u8('.'))
            return Err(Error::InvalidAppId(String::make(value)));
        String stem;
        bool   in_stem = true;
        for (auto byte : value.as_bytes()) {
            auto c = byte.to_primitive();
            if (! ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '.' || c == '_' || c == '-'))
                return Err(Error::InvalidAppId(String::make(value)));
            if (c == '.') in_stem = false;
            if (in_stem) stem.push_ascii(c >= 'A' && c <= 'Z' ? u8(c + ('a' - 'A')) : byte);
        }
        auto name = stem.as_str();
        bool reserved =
            name == "con"_str || name == "prn"_str || name == "aux"_str || name == "nul"_str;
        if (name.len() == usize(4)) {
            auto bytes = name.as_bytes();
            reserved = reserved || ((name.starts_with("com"_str) || name.starts_with("lpt"_str)) &&
                                    bytes[usize(3)] >= u8('1') && bytes[usize(3)] <= u8('9'));
        }
        if (reserved || value.len() > usize(255))
            return Err(Error::InvalidAppId(String::make(value)));
        return Ok(AppId(String::make(value)));
    }

    auto as_str() const noexcept -> ref<str> { return value_.as_str(); }
    auto clone() const -> AppId { return AppId(value_.clone()); }
};

struct AppRoots {
    Option<PathBuf> config, data, data_local, cache, state, runtime;
};
} // namespace rstd::dirs

export namespace rstd
{
template<>
struct Impl<fmt::Display, dirs::Error> : ImplBase<dirs::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        RSTD_MATCH(this->self()) {
            RSTD_CASE(InvalidAppId, value) {
                return formatter.write_fmt(
                    fmt::Arguments::make("invalid application id {:?}", value));
            }
            RSTD_CASE(InvalidPath, path) {
                return formatter.write_fmt(
                    fmt::Arguments::make("invalid directory path: {}", path.as_path()));
            }
            RSTD_CASE(Unsupported) {
                return formatter.write_str(
                    "directory convention is not supported on this platform"_str);
            }
            RSTD_CASE(System, cause) {
                return as<fmt::Display>(cause).fmt(formatter);
            }
            RSTD_CASE(Platform, message, code) {
                return formatter.write_fmt(
                    fmt::Arguments::make("{} (platform error {})", message, code));
            }
        }
    }
};

template<>
struct Impl<fmt::Debug, dirs::Error> : ImplBase<dirs::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, dirs::Error> : ImplBase<dirs::Error> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& value = this->self();
        if (! value.is_System()) return None();
        return Some(dyn<error::Error>::from_ref(value.as_System().cause));
    }
};
} // namespace rstd
