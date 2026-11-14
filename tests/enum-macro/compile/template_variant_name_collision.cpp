import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 template<class Value>
                 enum Invalid {
                     Value(int),
                 })]];
