export module rstd.tests.enum_macro.fixture;

import rstd.core;
import rstd.alloc;
import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;
using alloc::string::String;
using alloc::sync::Arc;

export namespace enum_macro::fixture
{
[[pmacro::invoke(
    "rstd::Enum",
    [[derive(Debug, Clone)]] enum Message {
        Unit,
        Tuple(i32, String),
        Named { count: i32, text: String },
        EmptyTuple(),
        EmptyRecord {},
        Wrapped(rstd::tuple<i32, i32>),
    } members {
    public:
        static auto custom() -> Message;
        auto        score() const -> i32;
    })]];

inline auto Message::custom() -> Message {
    return Named(i32(7), String::make("custom"_str));
}
inline auto Message::score() const -> i32 {
    return is_Named() ? as_Named().count : i32();
}

[[pmacro::invoke("rstd::Enum",
                 template<class T>
                 [[derive(Debug, Clone)]] enum Maybe {
                     [[default]] None,
                     Some(value : T),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 template<class First, typename Second>
                 enum Pair {
                     [[default]] Empty,
                     Value(First, Second),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 enum DefaultNumber {
                     Idle,
                     [[default]] Number(value : i32),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 enum NoDefault {
                     Unit,
                     Value(i32),
                 })]];

struct SharedPayload {
    i32 value;
};

[[pmacro::invoke("rstd::Enum",
                 enum DuplicatePayload {
                     First  = SharedPayload,
                     Second = SharedPayload,
                 })]];

struct [[pmacro::derive("rstd::Debug", "rstd::Clone", "rstd::Error"),
         pmacro::helper("rstd::Error", format("leaf {message}"))]] Leaf {
    String message;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("nested {cause}"))]] NestedLeaf {
    [[pmacro::helper("rstd::Error", source)]]
    Leaf cause;
};

[[pmacro::invoke(
    "rstd::Enum",
    [[derive(Debug, Clone, Error)]] enum Failure {
        [[error("unit")]] Unit,
        [[error("{cause}")]] Nested([[from]] cause : Leaf),
        [[error("{message}")]] Named { message: String },
        [[error("number {0}")]] Number(i32),
        [[error("{0}:{1}")]] Numbers(i32, i32),
        [[error("empty tuple")]] EmptyTuple(),
        [[error("empty record")]] EmptyRecord {},
    } members {
    public:
        static auto custom() -> Failure;
    })]];

inline auto Failure::custom() -> Failure {
    return Named(String::make("custom"_str));
}

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Error)]] enum SourceFailure {
                     [[error("{cause}")]] Shared([[source]] cause : Arc<Leaf>),
                     [[error("optional")]] Optional([[source]] cause : Option<Arc<Leaf>>),
                     [[transparent]] Transparent(NestedLeaf),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Clone, Error)]] enum Recursive {
                     [[error("root")]] Root,
                     [[error("{error}\n{context}")]] Context {
                         context: String,
                         [[source]] error: Arc<Recursive>,
                     },
                 })]];

struct [[pmacro::derive("rstd::Debug", "rstd::Display"),
         pmacro::helper("rstd::Display", format("{message}"))]] ManualPayload {
    String    message;
    Arc<Leaf> cause;
};
} // namespace enum_macro::fixture

export namespace rstd
{
template<>
struct Impl<error::Error, enum_macro::fixture::ManualPayload>
    : ImplBase<enum_macro::fixture::ManualPayload> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const enum_macro::fixture::Leaf& cause = *this->self().cause;
        return Some(rstd::dyn<error::Error>::from_ref(cause));
    }
};
} // namespace rstd

export namespace enum_macro::fixture
{
[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Error)]] enum AdaptedFailure {
                     Adapted = ManualPayload,
                 })]];

[[pmacro::invoke("rstd::Enum",
                 [[derive("serde::derive")]] enum External {
                     Unit,
                     Text(String),
                     Pair(i32, String),
                     Record { name: String, count: i32 },
                     EmptyTuple(),
                     EmptyRecord {},
                     Optional(Option<String>),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 [[derive("serde::derive")]] [[helper("serde::derive", untagged)]] enum ItemRef {
                     Id(i64),
                     Object { id: i64, name: String },
                 })]];

[[pmacro::invoke("rstd::Enum",
                 [[derive("serde::derive")]] [[helper("serde::derive", untagged)]] enum Number {
                     Integer(i64),
                     Floating(f64),
                 })]];

[[pmacro::invoke(
    "rstd::Enum",
    [[derive("serde::derive")]] [[helper("serde::derive", untagged)]] enum ObjectChoice {
        First { shared: i64, first: String },
        Second { shared: i64, second: String },
    })]];

struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", validate([](const auto& value) -> Result<empty, ref<str>> {
                            if (value.count == u64(99)) return Err("invalid count"_str);
                            return Ok(empty {});
                        }))]]
RequiredReference {
    [[pmacro::helper("serde::derive", alias("legacy"))]]
    ItemRef item;
    [[pmacro::helper("serde::derive", default)]]
    bool enabled { true };
    [[pmacro::helper("serde::derive", default)]]
    Option<String> note { Some("fallback"_Str) };
    [[pmacro::helper("serde::derive", default([] {
                         return u64(7);
                     }))]]
    u64 count;
    [[pmacro::helper("serde::derive", default([] {
                         return ItemRef::Id(i64(13));
                     }))]]
    ItemRef fallback;
    [[pmacro::helper("serde::derive", skip)]]
    i64 local { item.is_Id() ? i64(12) : i64(24) };
};

struct [[pmacro::derive("serde::derive")]] RequiredOrder {
    ItemRef first;
    ItemRef second;
};

[[pmacro::invoke("rstd::Enum",
                 [[derive("serde::derive")]] enum NestedReference {
                     Object { item: ItemRef },
                 })]];

struct ReferenceValidation {
    static inline unsigned calls {};
};

[[pmacro::invoke(
    "rstd::Enum",
    [[derive("serde::derive")]] [[helper("serde::derive",
                                         untagged,
                                         decode_only,
                                         validate(ValidatedRef::validate))]] enum ValidatedRef {
        Id(i64),
        Key { type: i64 },
        Fallback { type: i64 },
    } members {
    public:
        static auto validate(const ValidatedRef& value) -> Result<empty, ref<str>>;
    })]];

inline auto ValidatedRef::validate(const ValidatedRef& value) -> Result<empty, ref<str>> {
    ++ReferenceValidation::calls;
    if (value.is_Key() && value.as_Key().type != i64(1)) return Err("invalid key type"_str);
    return Ok(empty {});
}

[[pmacro::invoke("rstd::Enum",
                 [[derive("serde::derive")]] [[helper("serde::derive", untagged)]] enum Shapes {
                     Unit,
                     Tuple(i64, String),
                     Array(::alloc::vec::Vec<i64>),
                     Record {},
                 })]];

[[pmacro::invoke(
    "rstd::Enum",
    [[derive("serde::derive")]] [[helper("serde::derive", internal, tag("kind"))]] enum Internal {
        Unit,
        Record { name: String },
    })]];

[[pmacro::invoke("rstd::Enum",
                 [[derive("serde::derive")]]
                 [[helper("serde::derive", adjacent, tag("kind"), content("data"))]] enum Adjacent {
                     Unit,
                     Text(String),
                     Record { name: String },
                     Optional(Option<String>),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 [[derive("serde::derive")]] enum FieldOptions {
                     [[helper("serde::derive", ignore_unknown)]] Record {
                         [[helper("serde::derive", rename("wire_name"))]] name: String,
                         [[helper("serde::derive", default)]] note: Option<String>,
                     },
                 })]];

[[pmacro::invoke(
    "rstd::Enum",
    [[derive("serde::derive")]] [[helper("serde::derive", encode_only)]] enum EncodeOnly {
        Value(String),
    })]];

[[pmacro::invoke(
    "rstd::Enum",
    [[derive("serde::derive")]] [[helper("serde::derive", decode_only)]] enum DecodeOnly {
        Value(String),
    })]];

auto message_from_peer() -> Message;
auto failure_from_peer() -> Failure;
auto decode_from_peer(ref<str> input) -> Result<Adjacent, rstd::serde::Error>;
} // namespace enum_macro::fixture

namespace enum_macro::fixture::nested
{
export {
    [[pmacro::invoke("rstd::Enum",
                     [[derive("rstd::Debug", "rstd::Clone")]] enum ExportBlock {
                         [[default]] Idle,
                         Text(String),
                     })]];
}
} // namespace enum_macro::fixture::nested

namespace
{
[[pmacro::invoke("rstd::Enum",
                 enum LocalEnum {
                     Value(i32),
                 })]];
}

export auto enum_macro_local_value() -> i32 {
    return LocalEnum::Value(i32(9)).as_Value().field_0;
}
