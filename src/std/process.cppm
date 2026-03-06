export module rstd:process;
export import :sys;

namespace rstd::process
{

[[gnu::cold]] [[noreturn]]
void abort() {
    sys::abort_internal();
}

} // namespace rstd::process