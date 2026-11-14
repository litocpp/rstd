#include <rstd/enum.hpp>
import rstd.core;

struct Event {
    RSTD_ENUM(Event, (Selected), (Idle))
};
auto main() -> int {
    RSTD_MATCH(Event::Selected()) {
        RSTD_CASE(Selected) {
            break;
        }
        RSTD_CASE_DEFAULT {
            break;
        }
    }
    auto raw = Event::Tag::Selected;
    switch (raw) {
    case Event::Tag::Selected: return 0;
    }
    return 1;
}
