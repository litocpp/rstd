import rstd.core;

struct Base {};

struct [[pmacro::derive("rstd::Debug")]] WithBase : Base {
    int value;
};
