import rstd.serde;
enum class [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", newtype)]] Invalid
{
    Value
};
