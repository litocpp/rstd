import rstd.core;

enum class [[pmacro::derive("rstd::FromStr")]] [[pmacro::helper(
    "rstd::FromStr",
    ascii_case_insensitive)]] AliasCaseCollision
{
    First [[pmacro::helper("rstd::FromStr", alias("shared"))]],
    Second [[pmacro::helper("rstd::FromStr", alias("SHARED"))]],
};
