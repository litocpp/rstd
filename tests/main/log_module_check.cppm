export module rstd.tests.log_module_check;
import rstd.log;

using namespace rstd::literals;

namespace log_module_check
{

export void emit_with_target() noexcept {
    rstd::log::info(rstd::log::Target("rstd.tests.log_module_check"_str), "hello from a module");
}

} // namespace log_module_check
