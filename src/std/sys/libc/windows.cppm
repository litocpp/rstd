module;
#include <rstd/macro.hpp>

#ifdef RSTD_OS_WINDOWS
#include <windows.h>
#include <synchapi.h>
#endif
export module rstd:sys.libc.windows;

#ifdef RSTD_OS_WINDOWS
export namespace rstd::sys::libc
{

// ── Types ────────────────────────────────────────────────────────────────
using ::HANDLE;
using ::DWORD;
using ::BOOL;
using ::LARGE_INTEGER;
using ::FILETIME;
using ::SRWLOCK;
using ::CONDITION_VARIABLE;

// ── Constants ────────────────────────────────────────────────────────────
constexpr auto M_TRUE                              = TRUE;
constexpr auto M_FALSE                             = FALSE;
constexpr auto M_INFINITE                          = INFINITE;
constexpr auto M_ERROR_TIMEOUT                     = ERROR_TIMEOUT;
inline const auto M_INVALID_HANDLE_VALUE            = INVALID_HANDLE_VALUE;
constexpr auto M_WAIT_FAILED                       = WAIT_FAILED;
constexpr auto M_STD_INPUT_HANDLE                  = STD_INPUT_HANDLE;
constexpr auto M_STD_OUTPUT_HANDLE                 = STD_OUTPUT_HANDLE;
constexpr auto M_STD_ERROR_HANDLE                  = STD_ERROR_HANDLE;
constexpr auto M_STACK_SIZE_PARAM_IS_A_RESERVATION = STACK_SIZE_PARAM_IS_A_RESERVATION;
constexpr auto M_CP_UTF8                           = CP_UTF8;

// ── Error ────────────────────────────────────────────────────────────────
using ::GetLastError;

// ── Synchronization — SRWLock ────────────────────────────────────────────
using ::AcquireSRWLockExclusive;
using ::TryAcquireSRWLockExclusive;
using ::ReleaseSRWLockExclusive;

// ── Synchronization — Condition Variable ─────────────────────────────────
using ::SleepConditionVariableSRW;
using ::WakeConditionVariable;
using ::WakeAllConditionVariable;

// ── Synchronization — WaitOnAddress (futex) ──────────────────────────────
using ::WaitOnAddress;
using ::WakeByAddressSingle;
using ::WakeByAddressAll;

// ── Time ─────────────────────────────────────────────────────────────────
using ::QueryPerformanceFrequency;
using ::QueryPerformanceCounter;
using ::GetSystemTimeAsFileTime;

// ── Threading ────────────────────────────────────────────────────────────
using ::CreateThread;
using ::WaitForSingleObject;
using ::CloseHandle;
using ::GetCurrentThreadId;
using ::Sleep;
using ::SwitchToThread;
using ::GetCurrentThread;
using ::SetThreadDescription;

// ── IO ───────────────────────────────────────────────────────────────────
using ::GetStdHandle;
using ::WriteFile;
using ::ReadFile;

// ── String ───────────────────────────────────────────────────────────────
using ::MultiByteToWideChar;

// ── Process ──────────────────────────────────────────────────────────────
using ::RaiseFailFastException;

} // namespace rstd::sys::libc
#endif
