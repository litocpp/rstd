import rstd.serde;
struct [[pmacro::derive("serde::derive")]] Invalid {
    [[pmacro::helper("serde::derive", validate([] {
                         return true;
                     }))]]
    int value;
};
