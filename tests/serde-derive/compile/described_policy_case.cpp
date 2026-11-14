#include <rstd/enum.hpp>
import rstd.serde;
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive", described_enum, variant("Missing", ignore_unknown))]]
Event {
    RSTD_ENUM_TYPES(Event, (Idle))
};
