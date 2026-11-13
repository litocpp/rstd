import rstd.core;

struct [[pmacro::derive("rstd::TryFrom")]] Invalid {
    int value;
};
