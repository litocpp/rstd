module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <windows.h>
#endif

module rstd.sys.mem;
#if RSTD_OS_UNIX
import rstd.sys.libc;
#endif

namespace rstd::sys::mem
{

#if RSTD_OS_UNIX
auto information() noexcept -> Information {
    auto const size      = libc::sysconf(libc::SC_PAGESIZE);
    auto const page_size = usize(size > 0 ? static_cast<size_t>(size) : size_t(4096));
    return Information { page_size, page_size };
}

auto map(usize size) noexcept -> void* {
    auto* pointer = libc::mmap(nullptr,
                               size.to_primitive(),
                               libc::PROT_READ | libc::PROT_WRITE,
                               libc::MAP_PRIVATE | libc::MAP_ANONYMOUS,
                               -1,
                               0);
    return pointer == libc::MAP_FAILED ? nullptr : pointer;
}

auto unmap(void* pointer, usize size) noexcept -> bool {
    return libc::munmap(pointer, size.to_primitive()) == 0;
}
#elif RSTD_OS_WINDOWS
auto information() noexcept -> Information {
    SYSTEM_INFO information {};
    ::GetSystemInfo(&information);
    return Information {
        usize(static_cast<size_t>(information.dwPageSize)),
        usize(static_cast<size_t>(information.dwAllocationGranularity)),
    };
}

auto map(usize size) noexcept -> void* {
    return ::VirtualAlloc(nullptr, size.to_primitive(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

auto unmap(void* pointer, usize) noexcept -> bool {
    return ::VirtualFree(pointer, 0, MEM_RELEASE) != 0;
}
#endif

} // namespace rstd::sys::mem
