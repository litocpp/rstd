export module rstd.serde:error;
export import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::boxed::Box;
using ::alloc::string::String;
using ::alloc::vec::Vec;

export namespace rstd::serde
{

enum class ValueKind : rstd::uint8_t
{
    Null,
    Unit,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    Float,
    String,
    Bytes,
    Sequence,
    Map,
    Enum,
    Extension,
};

auto value_kind_name(ValueKind kind) noexcept -> ref<str>;

enum class PathSegmentKind : rstd::uint8_t
{
    Field,
    Index,
    MapKey,
    Variant,
};

class PathSegment {
    PathSegmentKind kind_;
    Option<String>  name_;
    usize           index_ {};

    PathSegment(PathSegmentKind kind, Option<String> name, usize index);

public:
    static auto field(ref<str> name) -> PathSegment;
    static auto index(usize value) -> PathSegment;
    static auto map_key(ref<str> name) -> PathSegment;
    static auto variant(ref<str> name) -> PathSegment;

    constexpr auto kind() const noexcept -> PathSegmentKind { return kind_; }
    constexpr auto index() const noexcept -> usize { return index_; }
    auto           name() const noexcept [[clang::lifetimebound]] -> Option<ref<str>> {
        if (name_.is_none()) return None();
        return Some(name_->as_str());
    }
    auto clone() const -> PathSegment;
};

class DataPath {
    Vec<PathSegment> segments_;

public:
    DataPath();
    explicit DataPath(Vec<PathSegment> segments);

    auto segments() const noexcept [[clang::lifetimebound]] -> slice<PathSegment> {
        return segments_.as_slice();
    }
    auto clone() const -> DataPath;
    auto with_field(ref<str> name) const -> DataPath;
    auto with_index(usize index) const -> DataPath;
    auto with_map_key(ref<str> name) const -> DataPath;
    auto with_variant(ref<str> name) const -> DataPath;
};

enum class ErrorKind : rstd::uint8_t
{
    TypeMismatch,
    MissingField,
    UnknownField,
    UnknownVariant,
    DuplicateField,
    InvalidValue,
    Invariant,
    Unsupported,
    UnexpectedEnd,
};

class Error {
    ErrorKind                      kind_;
    DataPath                       path_;
    Option<ValueKind>              expected_;
    Option<ValueKind>              actual_;
    Option<String>                 message_;
    Option<Box<dyn<error::Error>>> source_;

    Error(ErrorKind                      kind,
          DataPath                       path,
          Option<ValueKind>              expected,
          Option<ValueKind>              actual,
          Option<String>                 message,
          Option<Box<dyn<error::Error>>> source);

public:
    static auto type_mismatch(DataPath path, ValueKind expected, ValueKind actual) -> Error;
    static auto missing_field(DataPath path, ref<str> field) -> Error;
    static auto unknown_field(DataPath path, ref<str> field) -> Error;
    static auto unknown_variant(DataPath path, ref<str> variant) -> Error;
    static auto duplicate_field(DataPath path, ref<str> field) -> Error;
    static auto invalid_value(DataPath path, ref<str> message) -> Error;
    template<typename Source>
        requires Impled<mtp::rm_cvf<Source>, error::Error>
    static auto invalid_value_with_source(DataPath path, ref<str> message, Source source) -> Error {
        return Error(ErrorKind::InvalidValue,
                     rstd::move(path),
                     None(),
                     None(),
                     Some(String::make(message)),
                     Some(Box<dyn<error::Error>>::make(rstd::move(source))));
    }
    static auto invariant(DataPath path, ref<str> message) -> Error;
    static auto unsupported(DataPath path, ref<str> message) -> Error;
    static auto unexpected_end(DataPath path) -> Error;

    constexpr auto kind() const noexcept -> ErrorKind { return kind_; }
    auto path() const noexcept [[clang::lifetimebound]] -> const DataPath& { return path_; }
    constexpr auto expected() const noexcept -> Option<ValueKind> { return expected_; }
    constexpr auto actual() const noexcept -> Option<ValueKind> { return actual_; }
    auto           message() const noexcept [[clang::lifetimebound]] -> Option<ref<str>> {
        if (message_.is_none()) return None();
        return Some(message_->as_str());
    }
    auto source() const noexcept [[clang::lifetimebound]] -> Option<error::ErrorRef> {
        if (source_.is_none()) return None();
        return Some(source_->as_ref());
    }
};

} // namespace rstd::serde

namespace rstd
{

template<>
struct Impl<fmt::Display, serde::Error> : ImplBase<serde::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, serde::Error> : ImplBase<serde::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, serde::Error> : ImplBase<serde::Error> {
    auto source() const noexcept -> Option<error::ErrorRef> { return this->self().source(); }
};

} // namespace rstd
