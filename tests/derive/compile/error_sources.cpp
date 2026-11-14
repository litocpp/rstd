import rstd.core;
struct [[pmacro::derive("rstd::Error"), pmacro::helper("rstd::Error", format("bad"))]] Bad {
    [[pmacro::helper("rstd::Error", source)]]
    rstd::num::ParseIntError first;
    [[pmacro::helper("rstd::Error", source)]]
    rstd::num::ParseIntError second;
};
