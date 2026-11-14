import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Clone)]] enum Invalid {
                     [[helper("rstd::Clone", variants)]] Unit,
                 })]];
