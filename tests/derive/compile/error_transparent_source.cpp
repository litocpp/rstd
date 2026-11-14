import rstd.core;
struct [[pmacro::derive("rstd::Error"), pmacro::helper("rstd::Error", transparent)]] Bad {
    [[pmacro::helper("rstd::Error", source)]]
    rstd::num::ParseIntError cause;
};
