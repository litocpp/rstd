#include <rstd/enum.hpp>
import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 enum Event {
                     Selected,
                     Idle,
                 })]];

auto main() -> int {
    RSTD_MATCH(Event::Selected()) {
        RSTD_CASE(Selected) {
            return 0;
        }
    }
    return 1;
}
