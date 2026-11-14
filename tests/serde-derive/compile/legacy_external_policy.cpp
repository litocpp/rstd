#include <rstd/enum.hpp>
import rstd.serde;
class [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", variants, ignore_unknown)]]
Event {
    RSTD_ENUM_TYPES(Event, (Idle))
};
