module;
#include <rstd/macro.hpp>

#ifdef RSTD_OS_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <synchapi.h>
#include <time.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>

inline constexpr auto _ERROR_FILE_NOT_FOUND             = ERROR_FILE_NOT_FOUND;
inline constexpr auto _ERROR_PATH_NOT_FOUND             = ERROR_PATH_NOT_FOUND;
inline constexpr auto _ERROR_ACCESS_DENIED              = ERROR_ACCESS_DENIED;
inline constexpr auto _ERROR_CONNECTION_REFUSED         = ERROR_CONNECTION_REFUSED;
inline constexpr auto _ERROR_CONNECTION_ABORTED         = ERROR_CONNECTION_ABORTED;
inline constexpr auto _ERROR_NETNAME_DELETED            = ERROR_NETNAME_DELETED;
inline constexpr auto _ERROR_HOST_UNREACHABLE           = ERROR_HOST_UNREACHABLE;
inline constexpr auto _ERROR_NETWORK_UNREACHABLE        = ERROR_NETWORK_UNREACHABLE;
inline constexpr auto _ERROR_ADDRESS_ALREADY_ASSOCIATED = ERROR_ADDRESS_ALREADY_ASSOCIATED;
inline constexpr auto _ERROR_BROKEN_PIPE                = ERROR_BROKEN_PIPE;
inline constexpr auto _ERROR_NO_DATA                    = ERROR_NO_DATA;
inline constexpr auto _ERROR_FILE_EXISTS                = ERROR_FILE_EXISTS;
inline constexpr auto _ERROR_ALREADY_EXISTS             = ERROR_ALREADY_EXISTS;
inline constexpr auto _WAIT_TIMEOUT                     = WAIT_TIMEOUT;
inline constexpr auto _ERROR_SEM_TIMEOUT                = ERROR_SEM_TIMEOUT;
inline constexpr auto _ERROR_INVALID_PARAMETER          = ERROR_INVALID_PARAMETER;
inline constexpr auto _ERROR_INVALID_DATA               = ERROR_INVALID_DATA;
inline constexpr auto _ERROR_DIR_NOT_EMPTY              = ERROR_DIR_NOT_EMPTY;
inline constexpr auto _ERROR_DISK_FULL                  = ERROR_DISK_FULL;
inline constexpr auto _ERROR_SEEK                       = ERROR_SEEK;
inline constexpr auto _ERROR_NOT_READY                  = ERROR_NOT_READY;
inline constexpr auto _ERROR_BUSY                       = ERROR_BUSY;
inline constexpr auto _ERROR_POSSIBLE_DEADLOCK          = ERROR_POSSIBLE_DEADLOCK;
inline constexpr auto _ERROR_NOT_SAME_DEVICE            = ERROR_NOT_SAME_DEVICE;
inline constexpr auto _ERROR_TOO_MANY_LINKS             = ERROR_TOO_MANY_LINKS;
inline constexpr auto _ERROR_FILENAME_EXCED_RANGE       = ERROR_FILENAME_EXCED_RANGE;
inline constexpr auto _ERROR_NOT_ENOUGH_MEMORY          = ERROR_NOT_ENOUGH_MEMORY;
inline constexpr auto _ERROR_OUTOFMEMORY                = ERROR_OUTOFMEMORY;
inline constexpr auto _ERROR_NOT_SUPPORTED              = ERROR_NOT_SUPPORTED;
inline constexpr auto _ERROR_CALL_NOT_IMPLEMENTED       = ERROR_CALL_NOT_IMPLEMENTED;
inline constexpr auto _ERROR_IO_PENDING                 = ERROR_IO_PENDING;
inline constexpr auto _ERROR_LOCK_VIOLATION             = ERROR_LOCK_VIOLATION;
inline constexpr auto _LOCKFILE_FAIL_IMMEDIATELY        = LOCKFILE_FAIL_IMMEDIATELY;
inline constexpr auto _LOCKFILE_EXCLUSIVE_LOCK          = LOCKFILE_EXCLUSIVE_LOCK;
inline constexpr auto _DUPLICATE_SAME_ACCESS            = DUPLICATE_SAME_ACCESS;
inline constexpr auto _WSAEACCES                        = WSAEACCES;
inline constexpr auto _WSAECONNREFUSED                  = WSAECONNREFUSED;
inline constexpr auto _WSAECONNRESET                    = WSAECONNRESET;
inline constexpr auto _WSAEHOSTUNREACH                  = WSAEHOSTUNREACH;
inline constexpr auto _WSAENETUNREACH                   = WSAENETUNREACH;
inline constexpr auto _WSAECONNABORTED                  = WSAECONNABORTED;
inline constexpr auto _WSAENOTCONN                      = WSAENOTCONN;
inline constexpr auto _WSAEADDRINUSE                    = WSAEADDRINUSE;
inline constexpr auto _WSAEADDRNOTAVAIL                 = WSAEADDRNOTAVAIL;
inline constexpr auto _WSAENETDOWN                      = WSAENETDOWN;
inline constexpr auto _WSAEWOULDBLOCK                   = WSAEWOULDBLOCK;
inline constexpr auto _WSAETIMEDOUT                     = WSAETIMEDOUT;
inline constexpr auto _WSAEINVAL                        = WSAEINVAL;
inline constexpr auto _WSAENOBUFS                       = WSAENOBUFS;
inline constexpr auto _WSAEALREADY                      = WSAEALREADY;
inline constexpr auto _WSAEINPROGRESS                   = WSAEINPROGRESS;
inline constexpr auto _WSAEOPNOTSUPP                    = WSAEOPNOTSUPP;

#undef ERROR_FILE_NOT_FOUND
#undef ERROR_PATH_NOT_FOUND
#undef ERROR_ACCESS_DENIED
#undef ERROR_CONNECTION_REFUSED
#undef ERROR_CONNECTION_ABORTED
#undef ERROR_NETNAME_DELETED
#undef ERROR_HOST_UNREACHABLE
#undef ERROR_NETWORK_UNREACHABLE
#undef ERROR_ADDRESS_ALREADY_ASSOCIATED
#undef ERROR_BROKEN_PIPE
#undef ERROR_NO_DATA
#undef ERROR_FILE_EXISTS
#undef ERROR_ALREADY_EXISTS
#undef WAIT_TIMEOUT
#undef ERROR_SEM_TIMEOUT
#undef ERROR_INVALID_PARAMETER
#undef ERROR_INVALID_DATA
#undef ERROR_DIR_NOT_EMPTY
#undef ERROR_DISK_FULL
#undef ERROR_SEEK
#undef ERROR_NOT_READY
#undef ERROR_BUSY
#undef ERROR_POSSIBLE_DEADLOCK
#undef ERROR_NOT_SAME_DEVICE
#undef ERROR_TOO_MANY_LINKS
#undef ERROR_FILENAME_EXCED_RANGE
#undef ERROR_NOT_ENOUGH_MEMORY
#undef ERROR_OUTOFMEMORY
#undef ERROR_NOT_SUPPORTED
#undef ERROR_CALL_NOT_IMPLEMENTED
#undef ERROR_IO_PENDING
#undef ERROR_LOCK_VIOLATION
#undef LOCKFILE_FAIL_IMMEDIATELY
#undef LOCKFILE_EXCLUSIVE_LOCK
#undef DUPLICATE_SAME_ACCESS
#undef WSAEACCES
#undef WSAECONNREFUSED
#undef WSAECONNRESET
#undef WSAEHOSTUNREACH
#undef WSAENETUNREACH
#undef WSAECONNABORTED
#undef WSAENOTCONN
#undef WSAEADDRINUSE
#undef WSAEADDRNOTAVAIL
#undef WSAENETDOWN
#undef WSAEWOULDBLOCK
#undef WSAETIMEDOUT
#undef WSAEINVAL
#undef WSAENOBUFS
#undef WSAEALREADY
#undef WSAEINPROGRESS
#undef WSAEOPNOTSUPP
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
using ::SYSTEMTIME;
using ::SYSTEM_INFO;
using ::OVERLAPPED;
using ::SRWLOCK;
using ::CONDITION_VARIABLE;
using ::BY_HANDLE_FILE_INFORMATION;
using ::FILE_BASIC_INFO;
using ::WIN32_FIND_DATAW;
using ::STARTUPINFOW;
using ::PROCESS_INFORMATION;
using ::SECURITY_ATTRIBUTES;

// ── Constants ────────────────────────────────────────────────────────────
constexpr auto    M_TRUE                              = TRUE;
constexpr auto    M_FALSE                             = FALSE;
constexpr auto    M_INFINITE                          = INFINITE;
constexpr auto    M_ERROR_TIMEOUT                     = ERROR_TIMEOUT;
inline const auto M_INVALID_HANDLE_VALUE              = INVALID_HANDLE_VALUE;
constexpr auto    M_WAIT_FAILED                       = WAIT_FAILED;
constexpr auto    M_STD_INPUT_HANDLE                  = STD_INPUT_HANDLE;
constexpr auto    M_STD_OUTPUT_HANDLE                 = STD_OUTPUT_HANDLE;
constexpr auto    M_STD_ERROR_HANDLE                  = STD_ERROR_HANDLE;
constexpr auto    M_STACK_SIZE_PARAM_IS_A_RESERVATION = STACK_SIZE_PARAM_IS_A_RESERVATION;
constexpr auto    M_CP_UTF8                           = CP_UTF8;
constexpr auto    M_MB_ERR_INVALID_CHARS              = MB_ERR_INVALID_CHARS;
constexpr auto    M_WC_ERR_INVALID_CHARS              = WC_ERR_INVALID_CHARS;
constexpr auto    M_GENERIC_READ                      = GENERIC_READ;
constexpr auto    M_GENERIC_WRITE                     = GENERIC_WRITE;
constexpr auto    M_FILE_APPEND_DATA                  = FILE_APPEND_DATA;
constexpr auto    M_FILE_READ_ATTRIBUTES              = FILE_READ_ATTRIBUTES;
constexpr auto    M_FILE_SHARE_READ                   = FILE_SHARE_READ;
constexpr auto    M_FILE_SHARE_WRITE                  = FILE_SHARE_WRITE;
constexpr auto    M_FILE_SHARE_DELETE                 = FILE_SHARE_DELETE;
constexpr auto    M_CREATE_NEW                        = CREATE_NEW;
constexpr auto    M_CREATE_ALWAYS                     = CREATE_ALWAYS;
constexpr auto    M_OPEN_EXISTING                     = OPEN_EXISTING;
constexpr auto    M_OPEN_ALWAYS                       = OPEN_ALWAYS;
constexpr auto    M_TRUNCATE_EXISTING                 = TRUNCATE_EXISTING;
constexpr auto    M_FILE_ATTRIBUTE_READONLY           = FILE_ATTRIBUTE_READONLY;
constexpr auto    M_FILE_ATTRIBUTE_DIRECTORY          = FILE_ATTRIBUTE_DIRECTORY;
constexpr auto    M_FILE_ATTRIBUTE_REPARSE_POINT      = FILE_ATTRIBUTE_REPARSE_POINT;
constexpr auto    M_FILE_ATTRIBUTE_NORMAL             = FILE_ATTRIBUTE_NORMAL;
constexpr auto    M_INVALID_FILE_ATTRIBUTES           = INVALID_FILE_ATTRIBUTES;
constexpr auto    M_FILE_FLAG_BACKUP_SEMANTICS        = FILE_FLAG_BACKUP_SEMANTICS;
constexpr auto    M_FILE_FLAG_OPEN_REPARSE_POINT      = FILE_FLAG_OPEN_REPARSE_POINT;
constexpr auto    M_FILE_BEGIN                        = FILE_BEGIN;
constexpr auto    M_FILE_CURRENT                      = FILE_CURRENT;
constexpr auto    M_FILE_END                          = FILE_END;
constexpr auto    M_MOVEFILE_REPLACE_EXISTING         = MOVEFILE_REPLACE_EXISTING;
constexpr auto    M_SYMBOLIC_LINK_FLAG_DIRECTORY      = SYMBOLIC_LINK_FLAG_DIRECTORY;
constexpr auto    M_SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE =
    SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
constexpr auto M_FILE_NAME_NORMALIZED       = FILE_NAME_NORMALIZED;
constexpr auto M_VOLUME_NAME_DOS            = VOLUME_NAME_DOS;
constexpr auto M_FILE_BASIC_INFO_CLASS      = FileBasicInfo;
constexpr auto M_ERROR_HANDLE_EOF           = ERROR_HANDLE_EOF;
constexpr auto M_ERROR_NO_MORE_FILES        = ERROR_NO_MORE_FILES;
constexpr auto M_STARTF_USESTDHANDLES       = STARTF_USESTDHANDLES;
constexpr auto M_CREATE_UNICODE_ENVIRONMENT = CREATE_UNICODE_ENVIRONMENT;
constexpr auto M_HANDLE_FLAG_INHERIT        = HANDLE_FLAG_INHERIT;
constexpr auto M_WAIT_OBJECT_0              = WAIT_OBJECT_0;
constexpr auto M_STILL_ACTIVE               = STILL_ACTIVE;
constexpr auto M_CSTR_EQUAL                 = CSTR_EQUAL;
constexpr auto M_O_RDONLY                   = _O_RDONLY;
constexpr auto M_O_WRONLY                   = _O_WRONLY;
constexpr auto M_O_BINARY                   = _O_BINARY;

// ── Error ────────────────────────────────────────────────────────────────
using ::GetLastError;

inline constexpr auto ERROR_FILE_NOT_FOUND             = _ERROR_FILE_NOT_FOUND;
inline constexpr auto ERROR_PATH_NOT_FOUND             = _ERROR_PATH_NOT_FOUND;
inline constexpr auto ERROR_ACCESS_DENIED              = _ERROR_ACCESS_DENIED;
inline constexpr auto ERROR_CONNECTION_REFUSED         = _ERROR_CONNECTION_REFUSED;
inline constexpr auto ERROR_CONNECTION_ABORTED         = _ERROR_CONNECTION_ABORTED;
inline constexpr auto ERROR_NETNAME_DELETED            = _ERROR_NETNAME_DELETED;
inline constexpr auto ERROR_HOST_UNREACHABLE           = _ERROR_HOST_UNREACHABLE;
inline constexpr auto ERROR_NETWORK_UNREACHABLE        = _ERROR_NETWORK_UNREACHABLE;
inline constexpr auto ERROR_ADDRESS_ALREADY_ASSOCIATED = _ERROR_ADDRESS_ALREADY_ASSOCIATED;
inline constexpr auto ERROR_BROKEN_PIPE                = _ERROR_BROKEN_PIPE;
inline constexpr auto ERROR_NO_DATA                    = _ERROR_NO_DATA;
inline constexpr auto ERROR_FILE_EXISTS                = _ERROR_FILE_EXISTS;
inline constexpr auto ERROR_ALREADY_EXISTS             = _ERROR_ALREADY_EXISTS;
inline constexpr auto WAIT_TIMEOUT                     = _WAIT_TIMEOUT;
inline constexpr auto ERROR_SEM_TIMEOUT                = _ERROR_SEM_TIMEOUT;
inline constexpr auto ERROR_INVALID_PARAMETER          = _ERROR_INVALID_PARAMETER;
inline constexpr auto ERROR_INVALID_DATA               = _ERROR_INVALID_DATA;
inline constexpr auto ERROR_DIR_NOT_EMPTY              = _ERROR_DIR_NOT_EMPTY;
inline constexpr auto ERROR_DISK_FULL                  = _ERROR_DISK_FULL;
inline constexpr auto ERROR_SEEK                       = _ERROR_SEEK;
inline constexpr auto ERROR_NOT_READY                  = _ERROR_NOT_READY;
inline constexpr auto ERROR_BUSY                       = _ERROR_BUSY;
inline constexpr auto ERROR_POSSIBLE_DEADLOCK          = _ERROR_POSSIBLE_DEADLOCK;
inline constexpr auto ERROR_NOT_SAME_DEVICE            = _ERROR_NOT_SAME_DEVICE;
inline constexpr auto ERROR_TOO_MANY_LINKS             = _ERROR_TOO_MANY_LINKS;
inline constexpr auto ERROR_FILENAME_EXCED_RANGE       = _ERROR_FILENAME_EXCED_RANGE;
inline constexpr auto ERROR_NOT_ENOUGH_MEMORY          = _ERROR_NOT_ENOUGH_MEMORY;
inline constexpr auto ERROR_OUTOFMEMORY                = _ERROR_OUTOFMEMORY;
inline constexpr auto ERROR_NOT_SUPPORTED              = _ERROR_NOT_SUPPORTED;
inline constexpr auto ERROR_CALL_NOT_IMPLEMENTED       = _ERROR_CALL_NOT_IMPLEMENTED;
inline constexpr auto ERROR_IO_PENDING                 = _ERROR_IO_PENDING;
inline constexpr auto ERROR_LOCK_VIOLATION             = _ERROR_LOCK_VIOLATION;
inline constexpr auto WSAEACCES                        = _WSAEACCES;
inline constexpr auto WSAECONNREFUSED                  = _WSAECONNREFUSED;
inline constexpr auto WSAECONNRESET                    = _WSAECONNRESET;
inline constexpr auto WSAEHOSTUNREACH                  = _WSAEHOSTUNREACH;
inline constexpr auto WSAENETUNREACH                   = _WSAENETUNREACH;
inline constexpr auto WSAECONNABORTED                  = _WSAECONNABORTED;
inline constexpr auto WSAENOTCONN                      = _WSAENOTCONN;
inline constexpr auto WSAEADDRINUSE                    = _WSAEADDRINUSE;
inline constexpr auto WSAEADDRNOTAVAIL                 = _WSAEADDRNOTAVAIL;
inline constexpr auto WSAENETDOWN                      = _WSAENETDOWN;
inline constexpr auto WSAEWOULDBLOCK                   = _WSAEWOULDBLOCK;
inline constexpr auto WSAETIMEDOUT                     = _WSAETIMEDOUT;
inline constexpr auto WSAEINVAL                        = _WSAEINVAL;
inline constexpr auto WSAENOBUFS                       = _WSAENOBUFS;
inline constexpr auto WSAEALREADY                      = _WSAEALREADY;
inline constexpr auto WSAEINPROGRESS                   = _WSAEINPROGRESS;
inline constexpr auto WSAEOPNOTSUPP                    = _WSAEOPNOTSUPP;

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
using ::FileTimeToSystemTime;
using ::SystemTimeToFileTime;
using ::SystemTimeToTzSpecificLocalTime;

inline auto gmtime_utc(::time_t secs) noexcept -> ::tm {
    ::tm out {};
    ::gmtime_s(&out, &secs);
    return out;
}

// ── Threading ────────────────────────────────────────────────────────────
using ::CreateThread;
using ::WaitForSingleObject;
using ::CloseHandle;
using ::DuplicateHandle;
using ::GetCurrentProcess;
using ::GetCurrentThreadId;
using ::Sleep;
using ::SwitchToThread;
using ::GetCurrentThread;
using ::SetThreadDescription;
using ::GetSystemInfo;

// ── IO ───────────────────────────────────────────────────────────────────
using ::GetStdHandle;
using ::WriteFile;
using ::ReadFile;
using ::CreateFileW;
using ::SetFilePointerEx;
using ::FlushFileBuffers;
using ::SetEndOfFile;
using ::GetOverlappedResult;
using ::GetFileInformationByHandle;
using ::GetFileInformationByHandleEx;
using ::SetFileInformationByHandle;
using ::GetFileAttributesW;
using ::SetFileAttributesW;
using ::SetFileTime;
using ::DeleteFileW;
using ::RemoveDirectoryW;
using ::MoveFileExW;
using ::CreateHardLinkW;
using ::CreateSymbolicLinkW;
using ::GetFinalPathNameByHandleW;
using ::CreateDirectoryW;
using ::FindFirstFileW;
using ::FindNextFileW;
using ::FindClose;
using ::LockFileEx;
using ::UnlockFileEx;
inline constexpr auto LOCKFILE_FAIL_IMMEDIATELY = _LOCKFILE_FAIL_IMMEDIATELY;
inline constexpr auto LOCKFILE_EXCLUSIVE_LOCK   = _LOCKFILE_EXCLUSIVE_LOCK;
inline constexpr auto DUPLICATE_SAME_ACCESS     = _DUPLICATE_SAME_ACCESS;
using ::GetConsoleMode;
using ::_isatty;
using ::_fileno;

// ── String ───────────────────────────────────────────────────────────────
using ::MultiByteToWideChar;
using ::WideCharToMultiByte;

// ── Process ──────────────────────────────────────────────────────────────
using ::RaiseFailFastException;
using ::ExitProcess;
using ::GetCurrentProcessId;
using ::CreatePipe;
using ::SetHandleInformation;
using ::CreateProcessW;
using ::GetExitCodeProcess;
using ::TerminateProcess;
using ::GetEnvironmentStringsW;
using ::FreeEnvironmentStringsW;
using ::CompareStringOrdinal;
using ::GetEnvironmentVariableA;
using ::SetEnvironmentVariableA;

using ::_putenv_s;
using ::_open_osfhandle;
using ::_get_osfhandle;
using ::_close;

} // namespace rstd::sys::libc
#endif
