module rstd;

import :sys.thread.windows;

using rstd_alloc::boxed::Box;
using rstd::thread::ThreadInit;
using namespace rstd::sys::libc;

extern "C" DWORD __stdcall rstd_thread_start_win(void* data);

namespace rstd::sys::thread::windows
{

auto Thread::make(usize stack, Box<ThreadInit>&& init) -> rstd::io::Result<Thread> {
    auto raw = rstd::move(init).into_raw();

    auto handle = CreateThread(nullptr,
                               stack.to_primitive(),
                               rstd_thread_start_win,
                               raw.p,
                               M_STACK_SIZE_PARAM_IS_A_RESERVATION,
                               nullptr);

    if (handle != nullptr) return Ok(Thread { .handle = handle });

    Box<ThreadInit>::from_raw(raw);
    return Err(rstd::io::error::Error::from_raw_os_error((i32)GetLastError()));
}

auto Thread::join() const -> rstd::io::Result<voidp> {
    auto result = WaitForSingleObject(handle, M_INFINITE);
    if (result == M_WAIT_FAILED) {
        return Err(rstd::io::error::Error::from_raw_os_error((i32)GetLastError()));
    }
    return Ok(nullptr);
}

auto Thread::detach() const -> rstd::io::Result<i32> {
    if (CloseHandle(handle) == 0) {
        return Err(rstd::io::error::Error::from_raw_os_error((i32)GetLastError()));
    }
    return Ok(i32(0));
}

void Thread::set_name(ref<ffi::CStr> name) {
    auto* data   = name.as_ptr();
    int   length = MultiByteToWideChar(M_CP_UTF8, 0, data, -1, nullptr, 0);
    if (length <= 0) return;

    auto* buffer = static_cast<wchar_t*>(
        rstd::sys::libc::malloc(sizeof(wchar_t) * static_cast<rstd::size_t>(length)));
    if (! buffer) return;

    MultiByteToWideChar(M_CP_UTF8, 0, data, -1, buffer, length);
    SetThreadDescription(GetCurrentThread(), buffer);
    rstd::sys::libc::free(buffer);
}

void Thread::sleep(rstd::time::Duration dur) {
    auto milliseconds = dur.as_millis().to_primitive();
    if (milliseconds >= M_INFINITE) {
        Sleep(M_INFINITE - 1);
    } else {
        Sleep(static_cast<DWORD>(milliseconds));
    }
}

void Thread::yield_now() {
    SwitchToThread();
}

auto Thread::available_parallelism() -> Option<usize> {
    auto information = SYSTEM_INFO {};
    GetSystemInfo(&information);
    auto count = information.dwNumberOfProcessors;
    return count > 0 ? Some(usize(static_cast<rstd::size_t>(count))) : None();
}

} // namespace rstd::sys::thread::windows
