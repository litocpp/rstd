import rstd.serde;

struct [[pmacro::derive("serde::derive")]] Invalid {
    [[pmacro::helper("serde::derive", untagged)]]
    int value;
};
