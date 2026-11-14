import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug)]] [[helper("rstd::Debug", rename("Ignored"))]] enum Invalid {
                     Unit,
                 })]];
