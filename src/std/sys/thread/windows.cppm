module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <windows.h>
#endif
export module rstd:sys.thread.windows;

#if RSTD_OS_WINDOWS
export import :io;
export import :sys.libc.std;
export import :thread.thread;
export import rstd.alloc;

using rstd_alloc::boxed::Box;
using rstd::thread::ThreadInit;

extern "C" DWORD WINAPI rstd_thread_start_win(void* data);

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
            STACK_SIZE_PARAM_IS_A_RESERVATION,
            nullptr);

        if (h != nullptr) {
            return Ok(Thread { .handle = h });
        } else {
            Box<ThreadInit>::from_raw(raw);
            return Err(rstd::io::error::Error::from_raw_os_error((i32)GetLastError()));
        }
    }

    auto join() const -> rstd::io::Result<voidp> {
        auto rc = WaitForSingleObject(handle, INFINITE);
        if (rc == WAIT_FAILED) {
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
        // Convert UTF-8 CStr to wide string for SetThreadDescription
        auto* p = (const char*)name.p;
        int len = MultiByteToWideChar(CP_UTF8, 0, p, -1, nullptr, 0);
        if (len > 0) {
            auto* buf = (wchar_t*)rstd::sys::libc::malloc(sizeof(wchar_t) * (usize)len);
            if (buf) {
                MultiByteToWideChar(CP_UTF8, 0, p, -1, buf, len);
                SetThreadDescription(GetCurrentThread(), buf);
                rstd::sys::libc::free(buf);
            }
        }
    }

    static void sleep(rstd::time::Duration dur) {
        auto ms = dur.as_millis();
        if (ms >= INFINITE) {
            Sleep(INFINITE - 1);
        } else {
            Sleep(static_cast<DWORD>(ms));
        }
    }

    static void yield_now() { SwitchToThread(); }
};

} // namespace rstd::sys::thread::windows
#endif
