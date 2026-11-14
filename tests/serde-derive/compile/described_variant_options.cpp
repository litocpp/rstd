#include <rstd/enum.hpp>
import rstd.serde;
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive", described_enum, variant("Idle", tag("wire")))]] Event {
    RSTD_ENUM_TYPES(Event, (Idle))
};
