module;
#include <rstd/macro.hpp>
#include <new>

module rstd;

import :process;
import rstd.core;

using rstd::panic_::PanicInfo;
using namespace rstd::prelude;

extern "C" {

void* __rstd_alloc(usize size, usize align) {
    if (align != __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new(size, std::align_val_t { align }, std::nothrow_t {});
    } else {
        return ::operator new(size, std::nothrow_t {});
    }
}

void __rstd_dealloc(void* ptr, usize size, usize align) {
    if (align != __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        ::operator delete(ptr, size, std::align_val_t { align });
    } else {
        ::operator delete(ptr, size);
    }
}

void* __rstd_realloc(void* ptr, usize old_size, usize align, usize new_size) {
    void* new_ptr = __rstd_alloc(new_size, align);
    if (new_ptr) {
        rstd::mem::memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
        __rstd_dealloc(ptr, old_size, align);
    }
    return new_ptr;
}

void* __rstd_alloc_zeroed(usize size, usize align) {
    void* ptr = __rstd_alloc(size, align);
    if (ptr) {
        rstd::mem::memset(ptr, 0, size);
    }
    return ptr;
}

[[noreturn]]
void rstd_panic_impl(PanicInfo const& info) {
    // TODO: unwind

    auto& loc  = info.location;
    auto  file = rstd::str_::extract_last(loc.file_name(), 2);

    auto header = rstd::fmt::format(
        "thread 'main' panicked at {}:{}:{}:\n{}\n", file, loc.line(), loc.column(), info.message);
    rstd::io::Stderr out {};

    rstd::as<rstd::io::Write>(out).write((u8 const*)header.as_ref().as_raw_ptr(),
                                         header.as_ref().count_bytes());
    rstd::as<rstd::io::Write>(out).flush();
    rstd::process::abort();
}
}
