import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 template<class T>
                 requires true enum Invalid {
                     Unit,
                 })]];
