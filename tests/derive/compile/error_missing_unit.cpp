#include <rstd/enum.hpp>
import rstd.core;
class [[pmacro::derive("rstd::Error"), pmacro::helper("rstd::Error", variants)]] Bad {
    RSTD_ENUM_TYPES(Bad, (Unit))
};
