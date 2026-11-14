import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Clone)]] enum Invalid {
                     Value([[helper("rstd::Clone", variants)]] int),
                 })]];
