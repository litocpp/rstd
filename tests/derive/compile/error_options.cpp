import rstd.core;
struct [[pmacro::derive("rstd::Error"), pmacro::helper("rstd::Error", variants, format("bad"))]]
Bad {};
