#include <rstd/macro.hpp>
import rstd;

struct InnerError {};
struct OuterError {};
struct ConversionError {};

template<>
struct rstd::Impl<rstd::convert::TryFrom<InnerError>, OuterError> {
    using Error = ConversionError;

    static auto try_from(InnerError) -> rstd::Result<OuterError, Error> {
        return rstd::Ok(OuterError {});
    }
};

auto fallible_error_conversion() -> rstd::Result<long, OuterError> {
    auto value = rstd_try((rstd::Result<int, InnerError> { rstd::Err(InnerError {}) }));
    return rstd::Ok(static_cast<long>(value));
}

int main() {
    return fallible_error_conversion().is_ok() ? 0 : 1;
}
