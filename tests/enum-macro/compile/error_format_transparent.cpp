import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Error)]] enum Invalid {
                     [[error("invalid"), transparent]] Value(rstd::num::ParseIntError),
                 })]];
