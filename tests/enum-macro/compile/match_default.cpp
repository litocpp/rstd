#include <rstd/enum.hpp>
import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 enum Event {
                     Selected,
                     Idle,
                     Other,
                 })]];

auto test(const Event& value) -> int {
    RSTD_MATCH(value) {
        RSTD_CASE(Selected) {
            return 1;
        }
        RSTD_CASE_DEFAULT {
            return 2;
        }
    }
}
