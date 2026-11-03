module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

module rstd.sys.process.windows;
#if RSTD_OS_WINDOWS
import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::boxed::Box;
using rstd::os::handle::OwnedHandle;

namespace rstd::sys::process
{

auto last_error() -> io::error::Error {
    return io::error::Error::from_raw_os_error(i32(::GetLastError()));
}

auto child_pipe(bool child_reads) -> io::Result<ChildPipe> {
    static volatile LONG64 counter {};
    FILETIME               created {}, exited {}, kernel {}, user {};
    if (! ::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user))
        return Err(last_error());
    wchar_t name[] = L"\\\\.\\pipe\\rstd-0000000000000000-0000000000000000-0000000000000000";
    unsigned long long identity[] {
        static_cast<unsigned long long>(::GetCurrentProcessId()),
        (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime,
        static_cast<unsigned long long>(::InterlockedIncrement64(&counter)),
    };
    constexpr wchar_t digits[] = L"0123456789abcdef";
    for (unsigned part = 0; part < 3; ++part)
        for (unsigned index = 0; index < 16; ++index) {
            name[14 + part * 17 + 15 - index] = digits[identity[part] & 15];
            identity[part] >>= 4;
        }
    auto parent_raw = ::CreateNamedPipeW(
        name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,
        65536,
        65536,
        0,
        nullptr);
    if (parent_raw == INVALID_HANDLE_VALUE) return Err(last_error());
    auto parent = OwnedHandle::from_raw_handle(parent_raw);
    auto child_raw =
        ::CreateFileW(name,
                      (child_reads ? GENERIC_READ : GENERIC_WRITE) | FILE_READ_ATTRIBUTES,
                      0,
                      nullptr,
                      OPEN_EXISTING,
                      0,
                      nullptr);
    if (child_raw == INVALID_HANDLE_VALUE) return Err(last_error());
    auto       child = OwnedHandle::from_raw_handle(child_raw);
    OVERLAPPED connect {};
    if (! ::ConnectNamedPipe(parent_raw, &connect) && ::GetLastError() != ERROR_PIPE_CONNECTED)
        return Err(last_error());
    // CreateNamedPipe's write-only mode omits FILE_READ_ATTRIBUTES, which pipe validation needs.
    // Duplicate the duplex server with only the requested data direction and query access.
    HANDLE limited_raw {};
    if (! ::DuplicateHandle(::GetCurrentProcess(),
                            parent_raw,
                            ::GetCurrentProcess(),
                            &limited_raw,
                            (child_reads ? GENERIC_WRITE : GENERIC_READ) | FILE_READ_ATTRIBUTES,
                            FALSE,
                            0))
        return Err(last_error());
    return Ok(ChildPipe { OwnedHandle::from_raw_handle(limited_raw), rstd::move(child) });
}

struct InheritedHandles {
    HANDLE values[3] {};
    ~InheritedHandles() {
        for (auto handle : values)
            if (handle) ::CloseHandle(handle);
    }
};

struct ProcessAttributes {
    LPPROC_THREAD_ATTRIBUTE_LIST list {};
    ~ProcessAttributes() {
        if (list) {
            ::DeleteProcThreadAttributeList(list);
            ::HeapFree(::GetProcessHeap(), 0, list);
        }
    }
};

auto spawn_process(wchar_t*              command,
                   void*                 environment,
                   const wchar_t*        directory,
                   os::handle::RawHandle input,
                   os::handle::RawHandle output,
                   os::handle::RawHandle error) -> io::Result<SpawnedProcess> {
    InheritedHandles inherited;
    HANDLE           originals[] { input, output, error };
    HANDLE           handles[3] {};
    unsigned         count = 0;
    for (unsigned index = 0; index < 3; ++index) {
        auto original = originals[index];
        if (! original || original == INVALID_HANDLE_VALUE) continue;
        if (! ::DuplicateHandle(::GetCurrentProcess(),
                                original,
                                ::GetCurrentProcess(),
                                &inherited.values[index],
                                0,
                                TRUE,
                                DUPLICATE_SAME_ACCESS))
            return Err(last_error());
        handles[count++] = inherited.values[index];
    }
    STARTUPINFOEXW startup {};
    startup.StartupInfo.cb         = sizeof(startup);
    startup.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput  = inherited.values[0];
    startup.StartupInfo.hStdOutput = inherited.values[1];
    startup.StartupInfo.hStdError  = inherited.values[2];
    ProcessAttributes attributes;
    if (count) {
        SIZE_T bytes {};
        (void)::InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        auto list =
            static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(::HeapAlloc(::GetProcessHeap(), 0, bytes));
        if (! list)
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::OutOfMemory }));
        if (! ::InitializeProcThreadAttributeList(list, 1, 0, &bytes)) {
            auto result = last_error();
            ::HeapFree(::GetProcessHeap(), 0, list);
            return Err(rstd::move(result));
        }
        attributes.list = list;
        if (! ::UpdateProcThreadAttribute(list,
                                          0,
                                          PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                          handles,
                                          sizeof(HANDLE) * count,
                                          nullptr,
                                          nullptr))
            return Err(last_error());
        startup.lpAttributeList = list;
    }
    PROCESS_INFORMATION information {};
    if (! ::CreateProcessW(nullptr,
                           command,
                           nullptr,
                           nullptr,
                           count ? TRUE : FALSE,
                           CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                           environment,
                           directory,
                           &startup.StartupInfo,
                           &information))
        return Err(last_error());
    ::CloseHandle(information.hThread);
    return Ok(SpawnedProcess { OwnedHandle::from_raw_handle(information.hProcess),
                               u32(information.dwProcessId) });
}

struct WaitState {
    OwnedHandle           process;
    ProcessWait::Callback callback;
    void*                 context;
    HANDLE                registration {};
    WaitState(OwnedHandle process, ProcessWait::Callback callback, void* context)
        : process(rstd::move(process)), callback(callback), context(context) {}
};

void CALLBACK process_exited(void* context, BOOLEAN timeout) {
    if (! timeout) {
        auto* state = static_cast<WaitState*>(context);
        state->callback(state->context);
    }
}

auto ProcessWait::make(os::handle::BorrowedHandle process, Callback callback, void* context)
    -> io::Result<ProcessWait> {
    HANDLE duplicate {};
    if (! ::DuplicateHandle(::GetCurrentProcess(),
                            process.as_raw_handle(),
                            ::GetCurrentProcess(),
                            &duplicate,
                            0,
                            FALSE,
                            DUPLICATE_SAME_ACCESS))
        return Err(last_error());
    auto state = Box<WaitState>::make(OwnedHandle::from_raw_handle(duplicate), callback, context);
    if (! ::RegisterWaitForSingleObject(&state->registration,
                                        duplicate,
                                        process_exited,
                                        state.as_mut_ptr().as_raw_ptr(),
                                        INFINITE,
                                        WT_EXECUTEONLYONCE | WT_EXECUTEINWAITTHREAD))
        return Err(last_error());
    return Ok(ProcessWait { rstd::move(state).into_raw().p });
}

void ProcessWait::reset() noexcept {
    if (! state_) return;
    auto* state = static_cast<WaitState*>(state_);
    // No callback may access its context or process handle after this barrier.
    if (! ::UnregisterWaitEx(state->registration, INVALID_HANDLE_VALUE))
        rstd::panic { "process wait unregister failed" };
    auto owner = Box<WaitState>::from_raw(mut_ptr<WaitState>::from_raw_parts(state));
    state_     = nullptr;
}
ProcessWait::~ProcessWait() {
    reset();
}

} // namespace rstd::sys::process
#endif
