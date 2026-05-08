export module log_module_check;
import rstd.log;

namespace log_module_check
{

export void emit_from_module() noexcept {
    rstd::log::info("hello from a module");
}

} // namespace log_module_check
