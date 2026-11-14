import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Error)]] enum Invalid {
                     Unit,
                 })]];
