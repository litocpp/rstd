import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 template<class Self>
                 enum Invalid {
                     Value(Self),
                 })]];
