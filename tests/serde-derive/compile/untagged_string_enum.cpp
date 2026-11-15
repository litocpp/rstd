import rstd.serde;

enum class [[pmacro::derive("serde::derive"),
             pmacro::helper("serde::derive", untagged, string_enum)]] Invalid
{
    Value
};
