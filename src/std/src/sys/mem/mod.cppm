export module rstd.sys.mem;
import rstd.core;

export namespace rstd::sys::mem
{

struct Information {
    usize page_size;
    usize mapping_alignment;
};

auto information() noexcept -> Information;
auto map(usize size) noexcept -> void*;
auto unmap(void* pointer, usize size) noexcept -> bool;

} // namespace rstd::sys::mem
