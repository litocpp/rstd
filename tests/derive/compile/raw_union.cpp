import rstd.core;

union [[pmacro::derive("rstd::Debug")]] RawUnion {
    int    integer;
    double floating;
};
