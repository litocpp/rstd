import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Error)]] enum Invalid {
                     [[transparent(ignored_argument)]] Value(rstd::num::ParseIntError),
                 })]];
