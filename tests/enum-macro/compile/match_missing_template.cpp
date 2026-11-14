#include <rstd/enum.hpp>
import rstd.core;

[[pmacro::invoke("rstd::Enum",
                 template<class T>
                 enum Event {
                     Idle,
                     Value(T),
                 })]];

auto main() -> int {
    RSTD_MATCH(Event<int>::Value(3)) {
        RSTD_CASE(Value, number) {
            return number;
        }
    }
    return 1;
}
