#include <rstd/macro.hpp>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>

import rstd;

using namespace rstd::prelude;
namespace sig = rstd::signal;

static HANDLE      fallback_event;
static HANDLE      observer_event;
static BOOL WINAPI observer(DWORD kind) {
    if (kind == CTRL_C_EVENT) ::SetEvent(observer_event);
    return FALSE;
}
static BOOL WINAPI fallback(DWORD kind) {
    if (kind != CTRL_C_EVENT && kind != CTRL_BREAK_EVENT) return FALSE;
    ::SetEvent(fallback_event);
    return TRUE;
}

struct PendingRecv {
    using Output = rstd::async::signal::Recv::Output;
    rstd::async::signal::Recv future;
    HANDLE                    ready;
    auto poll(mut_ref<PendingRecv> self, rstd::task::Context& cx) -> rstd::task::Poll<Output> {
        auto result = rstd::future::poll(self->future, cx);
        if (result.is_pending()) ::SetEvent(self->ready);
        return result;
    }
};

static DWORD WINAPI send_interrupt(void* ready) {
    if (::WaitForSingleObject(ready, 10000) != WAIT_OBJECT_0) return 1;
    return ::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0) ? 0 : 2;
}

static int child(const char* name) {
    rstd_assert(::SetConsoleCtrlHandler(nullptr, FALSE));
    auto interrupt = sig::SignalKind::interrupt();
    if (std::strcmp(name, "initial") == 0 || std::strcmp(name, "default") == 0) {
        if (std::strcmp(name, "default") == 0) {
            auto subscription = sig::subscribe(interrupt).unwrap();
            subscription.close().unwrap();
        }
        rstd_assert(::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
        ::Sleep(10000);
        return 1;
    }
    if (std::strcmp(name, "invalid") == 0) {
        rstd_assert(interrupt.as_raw() == i32(CTRL_C_EVENT));
        rstd_assert(sig::SignalKind::from_raw(i32(CTRL_C_EVENT)).is_ok());
        for (int kind : array<int, 6> { -1, 1, 2, 5, 6, 64 }) {
            rstd_assert(sig::SignalKind::from_raw(i32(kind)).is_err());
        }
        rstd_assert(sig::unix::terminate().unwrap_err().kind().code ==
                    rstd::io::error::ErrorKind::Unsupported);
        rstd_assert(sig::unix::hangup().unwrap_err().kind().code ==
                    rstd::io::error::ErrorKind::Unsupported);
        rstd_assert(sig::unix::child().unwrap_err().kind().code ==
                    rstd::io::error::ErrorKind::Unsupported);
        rstd_assert(sig::unix::window_change().unwrap_err().kind().code ==
                    rstd::io::error::ErrorKind::Unsupported);
        rstd_assert(sig::unix::pipe().unwrap_err().kind().code ==
                    rstd::io::error::ErrorKind::Unsupported);
        rstd_assert(sig::unix::quit().unwrap_err().kind().code ==
                    rstd::io::error::ErrorKind::Unsupported);
        rstd_assert(sig::subscribe(slice<sig::SignalKind> {}).is_err());
        return 0;
    }
    if (std::strcmp(name, "subscriptions") == 0) {
        for (int i = 0; i < 30; ++i) {
            auto first  = sig::subscribe(interrupt).unwrap();
            auto second = sig::subscribe(interrupt).unwrap();
            rstd_assert(first.try_recv().unwrap_err().kind().code ==
                        rstd::io::error::ErrorKind::WouldBlock);
            rstd_assert(::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
            rstd_assert(first.recv().unwrap().unwrap() == interrupt);
            rstd_assert(second.recv().unwrap().unwrap() == interrupt);
            first.close().unwrap();
            first.close().unwrap();
            rstd_assert(first.recv().unwrap().is_none());
            rstd_assert(::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
            rstd_assert(second.recv().unwrap().unwrap() == interrupt);
            second.close().unwrap();
        }
        return 0;
    }
    if (std::strcmp(name, "forward") == 0) {
        fallback_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        rstd_assert(fallback_event && ::SetConsoleCtrlHandler(fallback, TRUE));
        auto subscription = sig::subscribe(interrupt).unwrap();
        rstd_assert(::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
        rstd_assert(subscription.recv().unwrap().unwrap() == interrupt);
        rstd_assert(::WaitForSingleObject(fallback_event, 0) == WAIT_TIMEOUT);
        rstd_assert(::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0));
        rstd_assert(::WaitForSingleObject(fallback_event, 10000) == WAIT_OBJECT_0);
        subscription.close().unwrap();
        rstd_assert(::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
        rstd_assert(::WaitForSingleObject(fallback_event, 10000) == WAIT_OBJECT_0);
        observer_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        rstd_assert(observer_event && ::SetConsoleCtrlHandler(observer, TRUE));
        auto reopened = sig::subscribe(interrupt).unwrap();
        rstd_assert(::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
        rstd_assert(reopened.recv().unwrap().unwrap() == interrupt);
        // Reinstalling our handler would move it ahead of the observer in the console chain.
        rstd_assert(::WaitForSingleObject(observer_event, 0) == WAIT_OBJECT_0);
        rstd_assert(::WaitForSingleObject(fallback_event, 0) == WAIT_TIMEOUT);
        reopened.close().unwrap();
        rstd_assert(::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
        rstd_assert(::WaitForSingleObject(fallback_event, 10000) == WAIT_OBJECT_0);
        return 0;
    }
    if (std::strcmp(name, "close") == 0) {
        auto subscription = sig::subscribe(interrupt).unwrap();
        auto receiver     = subscription.receiver().unwrap();
        rstd_assert(subscription.receiver().is_err());
        auto waiter = rstd::thread::builder::Builder::make()
                          .spawn([receiver = rstd::move(receiver)]() mutable {
                              rstd_assert(receiver.recv().unwrap().is_none());
                          })
                          .unwrap();
        subscription.close().unwrap();
        rstd::move(waiter).join().unwrap();
        return 0;
    }
    if (std::strcmp(name, "async") == 0) {
        auto ready = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        rstd_assert(ready);
        auto pending = rstd::async::signal::ctrl_c().unwrap();
        auto sender  = ::CreateThread(nullptr, 0, send_interrupt, ready, 0, nullptr);
        rstd_assert(sender);
        auto runtime = rstd::async::RuntimeBuilder::current_thread().build().unwrap();
        auto result  = runtime.block_on(PendingRecv { rstd::move(pending), ready });
        rstd_assert(result.unwrap().unwrap() == interrupt);
        rstd_assert(::WaitForSingleObject(sender, 10000) == WAIT_OBJECT_0);
        DWORD status;
        rstd_assert(::GetExitCodeThread(sender, &status) && status == 0);
        ::CloseHandle(sender);
        ::CloseHandle(ready);
        return 0;
    }
    return 2;
}

int main(int argc, char** argv) {
    if (argc == 2) return child(argv[1]);
    wchar_t executable[32768];
    auto    length = ::GetModuleFileNameW(nullptr, executable, 32768);
    rstd_assert(length > 0 && length < 32768);
    for (const wchar_t* name : array<const wchar_t*, 7> { L"initial",
                                                          L"default",
                                                          L"invalid",
                                                          L"subscriptions",
                                                          L"forward",
                                                          L"close",
                                                          L"async" }) {
        wchar_t command[33000];
        rstd_assert(::swprintf_s(command, L"\"%ls\" %ls", executable, name) > 0);
        STARTUPINFOW startup {};
        startup.cb          = sizeof(startup);
        startup.dwFlags     = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process {};
        // A fresh console confines broadcast Ctrl+C to this test child.
        rstd_assert(::CreateProcessW(executable,
                                     command,
                                     nullptr,
                                     nullptr,
                                     FALSE,
                                     CREATE_NEW_CONSOLE,
                                     nullptr,
                                     nullptr,
                                     &startup,
                                     &process));
        auto wait = ::WaitForSingleObject(process.hProcess, 20000);
        if (wait != WAIT_OBJECT_0) {
            ::TerminateProcess(process.hProcess, 125);
            ::WaitForSingleObject(process.hProcess, 5000);
        }
        DWORD status;
        rstd_assert(::GetExitCodeProcess(process.hProcess, &status));
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        auto expected = (::wcscmp(name, L"initial") == 0 || ::wcscmp(name, L"default") == 0)
                            ? DWORD(0xC000013A)
                            : DWORD(0);
        std::printf("signal %ls: exit 0x%08lx\n", name, status);
        rstd_assert(wait == WAIT_OBJECT_0 && status == expected);
    }
}
