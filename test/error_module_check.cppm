export module rstd.test.error_module_check;
import rstd.error;
import rstd;

export struct CrossModuleError {
    int value;
};

namespace rstd
{

template<>
struct Impl<fmt::Display, CrossModuleError> : ImplBase<CrossModuleError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "cross-module error";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Debug, CrossModuleError> : ImplBase<CrossModuleError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "CrossModuleError";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<error::Error, CrossModuleError> : DefaultInImpl<error::Error, CrossModuleError> {};

} // namespace rstd

export auto make_cross_module_error(int value) -> alloc::boxed::Box<rstd::dyn<rstd::error::Error>> {
    return alloc::boxed::Box<rstd::dyn<rstd::error::Error>>::make(CrossModuleError { value });
}
