import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug)]] [[debug(rename("Ignored"))]] enum Invalid {
                     Unit,
                 })]];
