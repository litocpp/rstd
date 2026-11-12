import rstd.core;

struct [[pmacro::derive("rstd::FromStr")]] MissingFromStrTransparent {
    rstd::i32 value;
};
