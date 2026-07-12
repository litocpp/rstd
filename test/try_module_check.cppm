module;
#include <rstd/macro.hpp>
export module try_module_check;
import rstd.core;

export auto try_module_check(bool success) -> rstd::Result<int, int> {
    auto source =
        success ? rstd::Result<int, int> { rstd::Ok(6) } : rstd::Result<int, int> { rstd::Err(7) };
    auto value = rstd_try(source);
    return rstd::Ok(value + 1);
}
