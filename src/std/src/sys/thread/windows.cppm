export module rstd:sys.thread.windows;

export import :io;
export import :sys.libc.std;
export import :sys.libc.windows;
export import :thread.thread;
export import rstd.alloc;

using rstd_alloc::boxed::Box;
using rstd::thread::ThreadInit;
using namespace rstd::sys::libc;

namespace rstd::sys::thread::windows
{

export struct Thread {
    HANDLE handle;

    static auto make(usize stack, Box<ThreadInit>&& init) -> rstd::io::Result<Thread>;
    auto        join() const -> rstd::io::Result<voidp>;
    auto        detach() const -> rstd::io::Result<i32>;
    static void set_name(ref<ffi::CStr> name);
    static void sleep(rstd::time::Duration dur);
    static void yield_now();
    static auto available_parallelism() -> Option<usize>;
};

} // namespace rstd::sys::thread::windows
