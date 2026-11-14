module;
#include <rstd/enum.hpp>
export module rstd.tests.serde_derive;
import rstd.serde;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace serde_derive::nested
{
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", newtype)]] Identifier {
    u64 value;
};
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", newtype)]] Text {
    String value;
};
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", newtype)]] OptionalText {
    Option<String> value;
};
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", ignore_unknown)]] Record {
    String name;
    [[pmacro::helper("serde::derive", default)]]
    Option<String> note;
};

struct FactoryState {
    static inline unsigned calls {};
    static inline unsigned validations {};
    static auto            next() -> u64 { return u64(++calls); }
    static auto            note() -> Option<String>;
};
auto FactoryState::note() -> Option<String> {
    return Some(String::make("fallback"_str));
}

struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", validate([](const auto& value) -> Result<empty, ref<str>> {
                            ++FactoryState::validations;
                            if (value.count == u64(99)) return Err("count must not be 99"_str);
                            return Ok(empty {});
                        }))]]
FactoryRecord {
    String name;
    [[pmacro::helper(
        "serde::derive", rename("wire"), alias("legacy"), default(FactoryState::next))]]
    u64 count;
    [[pmacro::helper("serde::derive", default(FactoryState::note))]]
    Option<String> note;
    [[pmacro::helper("serde::derive", default)]]
    bool enabled { true };
};

struct [[pmacro::derive("serde::derive")]] FactoryOrder {
    [[pmacro::helper("serde::derive", default(FactoryState::next))]]
    u64 first;
    [[pmacro::helper("serde::derive", default(FactoryState::next))]]
    u64 second;
    [[pmacro::helper("serde::derive", default([] {
                         return u64(17);
                     }))]]
    u64 expression;
};

class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive", variants, internal, tag("kind"))]] ValidatedInternal {
    RSTD_ENUM_TYPES(ValidatedInternal, (Record, (::serde_derive::nested::FactoryRecord)))
};
class [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", variants)]] External {
    RSTD_ENUM_TYPES(External,
                    (Idle),
                    (Text, (::serde_derive::nested::Text)),
                    (Record, (::serde_derive::nested::Record)))
};
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive", variants, internal, tag("kind"))]] Internal {
    RSTD_ENUM_TYPES(Internal, (Idle), (Record, (::serde_derive::nested::Record)))
};
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive", variants, tag("kind"), ignore_unknown)]] FlexibleInternal {
    RSTD_ENUM_TYPES(FlexibleInternal, (Idle), (Record, (::serde_derive::nested::Record)))
};
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive", variants, adjacent, tag("kind"), content("data"))]]
Adjacent {
    RSTD_ENUM_TYPES(Adjacent,
                    (Idle),
                    (Text, (::serde_derive::nested::Text)),
                    (Record, (::serde_derive::nested::Record)),
                    (Optional, (OptionalText)))
};
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive", variants, tag("kind"), content("data"), ignore_unknown)]]
FlexibleAdjacent {
    RSTD_ENUM_TYPES(FlexibleAdjacent, (Idle), (Record, (::serde_derive::nested::Record)))
};
enum class [[pmacro::derive("serde::derive"),
             pmacro::helper("serde::derive", string_enum)]] StringState
{
    Ready [[pmacro::helper("serde::derive", rename("ready"))]],
    Done
};
enum class [[pmacro::derive("serde::derive")]] EnumState
{
    Ready [[pmacro::helper("serde::derive", rename("ready"))]],
    Done
};
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", newtype, encode_only)]]
EncodeOnly {
    u64 value;
};
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", newtype, decode_only)]]
DecodeOnly {
    u64 value;
};
class [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", variants, encode_only)]]
EncodeVariants {
    RSTD_ENUM_TYPES(EncodeVariants, (Idle))
};
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive", variants, tag("kind"), content("data"), decode_only)]]
DecodeVariants {
    RSTD_ENUM_TYPES(DecodeVariants, (Idle))
};
} // namespace serde_derive::nested
