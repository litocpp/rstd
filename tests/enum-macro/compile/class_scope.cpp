import rstd.core;

struct Container {
    [[pmacro::invoke("rstd::Enum",
                     enum Invalid {
                         Value,
                     })]];
};
