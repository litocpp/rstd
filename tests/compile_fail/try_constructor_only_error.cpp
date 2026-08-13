#include <rstd/macro.hpp>
import rstd;

struct InnerError {};

struct OuterError {
    OuterError(InnerError&&) {}
};

auto constructor_only_error() -> rstd::Result<long, OuterError> {
    auto value = rstd_try((rstd::Result<int, InnerError> { rstd::Err(InnerError {}) }));
    return rstd::Ok(static_cast<long>(value));
}

int main() {
    return constructor_only_error().is_ok() ? 0 : 1;
}
