#include <rstd/macro.hpp>
import rstd;

auto unsupported_source() -> int {
    return rstd_try(42);
}

int main() {
    return unsupported_source();
}
