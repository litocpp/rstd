import rstd.serde;
struct [[pmacro::derive("serde::derive")]] Invalid {
    [[pmacro::helper("serde::derive", default, default([] {
                         return 1;
                     }))]]
    int value;
};
