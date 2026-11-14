import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug)]] enum Invalid {
                     [[helper("rstd::Debug", variants)]] Unit,
                 })]];
