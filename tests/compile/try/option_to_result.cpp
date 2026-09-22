#include <rstd/macro.hpp>
import rstd;

auto option_to_result() -> rstd::Result<int, int> {
    auto value = rstd_try(rstd::Option<int> {});
    return rstd::Ok(value);
}

int main() {
    return option_to_result().is_ok() ? 0 : 1;
}
