#include <rstd/enum.hpp>
import rstd.core;

struct Event {
    RSTD_ENUM(Event, (Selected), (Idle), (Stopped))
};
auto main() -> int {
    RSTD_MATCH(Event::Selected()) {
        RSTD_CASE(Selected) {
            return 0;
        }
        RSTD_CASE_DEFAULT {
            return 1;
        }
    }
}
