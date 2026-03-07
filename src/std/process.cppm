export module rstd:process;
export import :sys;

export namespace rstd::process
{

[[gnu::cold]] [[noreturn]]
void abort() {
    sys::abort_internal();
}

} // namespace rstd::process