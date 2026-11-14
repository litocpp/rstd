import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Error)]] enum Invalid {
                     [[error("invalid")]] Value {
                         [[source]] first: rstd::num::ParseIntError,
                         [[source]] second: rstd::num::ParseIntError,
                     },
                 })]];
