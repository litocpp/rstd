import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 [[derive(Display)]] enum Invalid {
                     [[display("{0}")]] Value([[helper("rstd::Display", ignored_option)]] int),
                 })]];
