#include <rstd/macro.hpp>
import rstd;

auto result_to_option() -> rstd::Option<int> {
    auto value = rstd_try((rstd::Result<int, int> { rstd::Err(4) }));
    return rstd::Some(value);
}

int main() {
    return result_to_option().is_some() ? 0 : 1;
}
