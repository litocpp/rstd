export module rstd:alloc.unix;
import rstd.core;

export namespace rstd::sys::alloc::unix
{

struct Information {
    usize page_size;
    usize mapping_alignment;
};

auto information() noexcept -> Information;
auto map(usize size) noexcept -> void*;
auto unmap(void* pointer, usize size) noexcept -> bool;

} // namespace rstd::sys::alloc::unix
