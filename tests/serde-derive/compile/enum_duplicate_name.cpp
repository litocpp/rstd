import rstd.serde;
enum class [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", string_enum)]] Invalid
{
    First [[pmacro::helper("serde::derive", rename("same"))]],
    Second [[pmacro::helper("serde::derive", rename("same"))]]
};
