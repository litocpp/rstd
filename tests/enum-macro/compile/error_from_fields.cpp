import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Error)]] enum Invalid {
                     [[error("invalid")]] Value([[from]] rstd::num::ParseIntError, int),
                 })]];
