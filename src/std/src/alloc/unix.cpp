module;
#include <sys/mman.h>
#include <unistd.h>

module rstd;
import :alloc.unix;

namespace rstd::sys::alloc::unix
{

auto information() noexcept -> Information {
    auto const size      = ::sysconf(_SC_PAGESIZE);
    auto const page_size = usize(size > 0 ? static_cast<size_t>(size) : size_t(4096));
    return Information { page_size, page_size };
}

auto map(usize size) noexcept -> void* {
#if defined(MAP_ANONYMOUS)
    constexpr auto anonymous = MAP_ANONYMOUS;
#else
    constexpr auto anonymous = MAP_ANON;
#endif
    auto* pointer = ::mmap(
        nullptr, size.to_primitive(), PROT_READ | PROT_WRITE, MAP_PRIVATE | anonymous, -1, 0);
    return pointer == MAP_FAILED ? nullptr : pointer;
}

auto unmap(void* pointer, usize size) noexcept -> bool {
    return ::munmap(pointer, size.to_primitive()) == 0;
}

} // namespace rstd::sys::alloc::unix
