import rstd.serde;
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", newtype, encode_only, decode_only)]] Invalid {
    int value;
};
