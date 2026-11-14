import rstd.core;

struct [[pmacro::derive("rstd::Clone")]] Record {
    int& value;
};
