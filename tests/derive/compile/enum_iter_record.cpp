import rstd.core;

struct [[pmacro::derive("rstd::EnumIter")]] Invalid {
    int value;
};
