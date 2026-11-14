import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Error)]] enum Invalid {
                     [[transparent]] Value(rstd::num::ParseIntError, int),
                 })]];
