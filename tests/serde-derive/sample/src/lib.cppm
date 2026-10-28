export module serde.sample;
import rstd;
import rstd.serde;
export namespace sample
{
using namespace rstd::prelude;
enum class [[pmacro::derive("serde::derive")]] Mode : rstd::int64_t
{
    Fast,
    Slow [[pmacro::helper("serde::derive", rename("slow"))]] = -7,
    Huge                                                     = 9007199254740993LL
};
struct [[pmacro::derive("serde::derive")]] Config {
    [[pmacro::helper("serde::derive", rename("label"), alias("name"))]]
    String         name;
    Mode           mode;
    Vec<u32>       values;
    Option<String> note;
    [[pmacro::helper("serde::derive", default)]]
    u32 count = u32(7);
    [[pmacro::helper("serde::derive", skip)]]
    u32 hidden = u32(9);
};
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", decode_only)]] InputOnly {
    u32 value;
};
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", encode_only)]]
OutputOnly {
    u32 value;
};
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", ignore_unknown)]] Loose {
    u32 value;
};
struct [[pmacro::derive("serde::derive")]] Special {
    [[pmacro::helper("serde::derive", rename("__proto__"))]]
    String value;
};
} // namespace sample
