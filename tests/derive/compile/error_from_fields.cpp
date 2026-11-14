import rstd.core;
struct [[pmacro::derive("rstd::Error"), pmacro::helper("rstd::Error", format("bad"))]] Bad {
    [[pmacro::helper("rstd::Error", from)]]
    rstd::num::ParseIntError cause;
    int                      unrelated;
};
