import rstd.core;

struct [[pmacro::derive("rstd::Display")]] MissingDisplayFormat {
    rstd::i32 value;
};
