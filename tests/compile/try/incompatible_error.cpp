#include <rstd/macro.hpp>
import rstd;

struct InnerError {};
struct OuterError {};

auto incompatible_error() -> rstd::Result<long, OuterError> {
    auto value = rstd_try((rstd::Result<int, InnerError> { rstd::Err(InnerError {}) }));
    return rstd::Ok(static_cast<long>(value));
}

int main() {
    return incompatible_error().is_ok() ? 0 : 1;
}
