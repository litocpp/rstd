import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, Error)]] enum Invalid {
                     [[error("{missing}")]] Value(present : int),
                 })]];
