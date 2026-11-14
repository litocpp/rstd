#include <rstd/enum.hpp>
import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 enum Event {
                     Selected,
                     Idle,
                 })]];

enum class Raw
{
    First,
    Second
};

auto test(Event value, Raw raw) -> int {
    RSTD_MATCH(value) {
        RSTD_CASE(Selected) {
            break;
        }
        RSTD_CASE_DEFAULT {
            break;
        }
    }
    switch (raw) {
    case Raw::First: return 1;
    default: return 2;
    }
}
