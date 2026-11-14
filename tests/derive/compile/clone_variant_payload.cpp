#include <rstd/enum.hpp>
import rstd.core;

struct MoveOnly {
    MoveOnly(MoveOnly&&)                    = default;
    auto operator=(MoveOnly&&) -> MoveOnly& = default;
};

class [[pmacro::derive("rstd::Clone"), pmacro::helper("rstd::Clone", variants)]] Event {
    RSTD_ENUM_TYPES(Event, (Idle), (Resource, (MoveOnly)))
};

auto duplicate(const Event& value) -> Event {
    return rstd::as<rstd::clone::Clone>(value).clone();
}
