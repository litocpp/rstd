import rstd.serde;
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", newtype)]] Invalid {
    [[pmacro::helper("serde::derive", default)]]
    int value;
};
