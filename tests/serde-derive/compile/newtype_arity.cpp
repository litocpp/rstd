import rstd.serde;
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", newtype)]] Invalid {
    int first;
    int second;
};
