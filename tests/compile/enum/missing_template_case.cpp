#include <rstd/enum.hpp>
import rstd.core;

struct Event {
    RSTD_ENUM(Event, (Selected), (Idle))
};
template<class T>
auto evaluate(T value) -> int {
    RSTD_MATCH(value) {
        RSTD_CASE(Selected) {
            return 0;
        }
    }
    return 1;
}
auto main() -> int {
    return evaluate(Event::Selected());
}
