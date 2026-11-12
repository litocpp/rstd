import rstd.core;

struct [[pmacro::derive("rstd::Debug")]] Bitfield {
    unsigned value : 3;
};
