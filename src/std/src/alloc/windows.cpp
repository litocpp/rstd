module;
#include <windows.h>

module rstd;
import :alloc.windows;

namespace rstd::sys::alloc::windows
{

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

} // namespace rstd::sys::alloc::windows
