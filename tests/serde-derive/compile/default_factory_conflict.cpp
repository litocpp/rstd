import rstd.serde;
struct [[pmacro::derive("serde::derive")]] Invalid {
    [[pmacro::helper("serde::derive",
                     default([] {
                         return 1;
                     }),
                     skip)]]
    int value;
};
