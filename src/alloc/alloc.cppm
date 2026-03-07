export module rstd.alloc:alloc;
export import rstd.basic;

using namespace rstd;

export {
    extern "C" {
    auto __rstd_alloc(usize size, usize align) -> u8*;
    void __rstd_dealloc(u8* ptr, usize size, usize align);
    auto __rstd_realloc(u8* ptr, usize old_size, usize align, usize new_size) -> u8*;
    auto __rstd_alloc_zeroed(usize size, usize align) -> u8*;
    }
}

namespace alloc
{

} // namespace alloc