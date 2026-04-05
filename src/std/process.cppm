export module rstd:process;
export import :sys;

export namespace rstd::process
{

/// Terminates the process abnormally via the platform abort mechanism.
[[gnu::cold]] [[noreturn]]
void abort() {
    sys::abort_internal();
}

/// Terminates the current process with the specified exit code.
///
/// This calls `_exit` on Unix and `ExitProcess` on Windows.
/// Destructors and atexit handlers are NOT run.
///
/// \param code  Exit status code (0 = success).
[[gnu::cold]] [[noreturn]]
void exit(i32 code) {
    sys::exit_internal(code);
}

/// Returns the OS-assigned process identifier of the current process.
auto id() -> u32 {
    return sys::getpid_internal();
}

} // namespace rstd::process
