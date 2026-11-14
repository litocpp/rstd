import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 template<class rstd_debug_definition>
                 [[derive(Debug)]] enum Invalid {
                     Value(rstd_debug_definition),
                 })]];
