#include <rstd/enum.hpp>
import rstd.core;
struct [[pmacro::derive("rstd::Debug", "rstd::Error"), pmacro::helper("rstd::Error", transparent)]]
Payload {
    [[pmacro::helper("rstd::Error", from)]]
    rstd::num::ParseIntError cause;
};
class [[pmacro::derive("rstd::Debug", "rstd::Error"),
        pmacro::helper("rstd::Debug", variants),
        pmacro::helper("rstd::Error", variants)]] Bad {
    RSTD_ENUM_TYPES(Bad, (First, (Payload)), (Second, (Payload)))
};
