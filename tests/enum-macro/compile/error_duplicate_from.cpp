import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Error)]] enum Invalid {
                     [[error("first")]] First([[from]] rstd::num::ParseIntError),
                     [[error("second")]] Second([[from]] rstd::num::ParseIntError),
                 })]];
