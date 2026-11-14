module;
#include <rstd/enum.hpp>
export module rstd.tests.serde_derive;
import rstd.serde;
import rstd;

using namespace rstd::prelude;

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
