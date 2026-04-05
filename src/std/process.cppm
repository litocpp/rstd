export module rstd:process;
export import :sys;

export namespace rstd::process
{

/// Terminates the process abnormally via the platform abort mechanism.
[[gnu::cold]] [[noreturn]]
void abort() {
    sys::abort_internal();
}

} // namespace rstd::process