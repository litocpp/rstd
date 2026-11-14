import rstd.core;
struct [[pmacro::derive("rstd::Error"), pmacro::helper("rstd::Error", format("bad\x41"))]] Bad {};
