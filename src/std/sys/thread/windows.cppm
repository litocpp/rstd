module;
#include <rstd/macro.hpp>
export module rstd:sys.thread.windows;

#if RSTD_OS_WINDOWS
export import :io;
export import :sys.libc.std;
export import :sys.libc.windows;
export import :thread.thread;
export import rstd.alloc;

using rstd_alloc::boxed::Box;
using rstd::thread::ThreadInit;
using namespace rstd::sys::libc;

extern "C" DWORD __stdcall rstd_thread_start_win(void* data);

namespace rstd::sys::thread::windows
{

export struct Thread {
    HANDLE handle;

    static auto make(usize stack, Box<ThreadInit>&& init) -> rstd::io::Result<Thread> {
        auto raw = rstd::move(init).into_raw();

        auto h = CreateThread(
            nullptr,
            stack,
            rstd_thread_start_win,
            raw.p,
            M_STACK_SIZE_PARAM_IS_A_RESERVATION,
            nullptr);

        if (h != nullptr) {
            return Ok(Thread { .handle = h });
        } else {
            Box<ThreadInit>::from_raw(raw);
            return Err(rstd::io::error::Error::from_raw_os_error((i32)GetLastError()));
        }
    }

    auto join() const -> rstd::io::Result<voidp> {
        auto rc = WaitForSingleObject(handle, M_INFINITE);
        if (rc == M_WAIT_FAILED) {
            return Err(
                rstd::io::error::Error::from_raw_os_error((i32)GetLastError()));
        }
        return Ok(nullptr);
    }

    auto detach() const -> rstd::io::Result<i32> {
        if (CloseHandle(handle) == 0) {
            return Err(
                rstd::io::error::Error::from_raw_os_error((i32)GetLastError()));
        }
        return Ok(0);
    }

    static void set_name(ref<ffi::CStr> name) {
        auto* p = (const char*)name.p;
        int len = MultiByteToWideChar(M_CP_UTF8, 0, p, -1, nullptr, 0);
        if (len > 0) {
            auto* buf = (wchar_t*)rstd::sys::libc::malloc(sizeof(wchar_t) * (usize)len);
            if (buf) {
                MultiByteToWideChar(M_CP_UTF8, 0, p, -1, buf, len);
                SetThreadDescription(GetCurrentThread(), buf);
                rstd::sys::libc::free(buf);
            }
        }
    }

    static void sleep(rstd::time::Duration dur) {
        auto ms = dur.as_millis();
        if (ms >= M_INFINITE) {
            Sleep(M_INFINITE - 1);
        } else {
            Sleep(static_cast<DWORD>(ms));
        }
    }

    static void yield_now() { SwitchToThread(); }
};

} // namespace rstd::sys::thread::windows
#endif
