module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>
export module rstd.tests.try_module_check;
import rstd.core;

export struct TryModuleInnerError {
    int value;
};

export class TryModuleOuterError {
    RSTD_ENUM(TryModuleOuterError, (Child, (TryModuleInnerError source;)))
};

namespace rstd
{

template<>
struct Impl<convert::From<TryModuleInnerError>, TryModuleOuterError> {
    static auto from(TryModuleInnerError error) -> TryModuleOuterError {
        return TryModuleOuterError::Child(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, TryModuleInnerError> : ImplBase<TryModuleInnerError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("module inner", sizeof("module inner") - 1);
    }
};

template<>
struct Impl<fmt::Debug, TryModuleInnerError> : ImplBase<TryModuleInnerError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("TryModuleInnerError", sizeof("TryModuleInnerError") - 1);
    }
};

template<>
struct Impl<error::Error, TryModuleInnerError> : DefaultInImpl<error::Error, TryModuleInnerError> {
};

template<>
struct Impl<fmt::Display, TryModuleOuterError> : ImplBase<TryModuleOuterError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("module outer", sizeof("module outer") - 1);
    }
};

template<>
struct Impl<fmt::Debug, TryModuleOuterError> : ImplBase<TryModuleOuterError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("TryModuleOuterError", sizeof("TryModuleOuterError") - 1);
    }
};

template<>
struct Impl<error::Error, TryModuleOuterError> : ImplBase<TryModuleOuterError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        return Some(dyn<error::Error>::from_ref(this->self().as_Child().source));
    }
};

} // namespace rstd

export auto try_module_check(bool success) -> rstd::Result<int, int> {
    auto source =
        success ? rstd::Result<int, int> { rstd::Ok(6) } : rstd::Result<int, int> { rstd::Err(7) };
    auto value = rstd_try(source);
    return rstd::Ok(value + 1);
}

export auto try_module_error_conversion() -> rstd::Result<int, TryModuleOuterError> {
    auto source = rstd::Result<int, TryModuleInnerError> {
        rstd::Err(TryModuleInnerError { 23 }),
    };
    auto value = rstd_try(source);
    return rstd::Ok(value);
}
