import rstd.core;

struct [[pmacro::derive("rstd::Debug")]] [[pmacro::helper("rstd::Debug", unknownhelper)]]
UnknownHelper {
    int value;
};
