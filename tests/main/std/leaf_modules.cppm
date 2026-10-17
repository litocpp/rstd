module;
#include <rstd/macro.hpp>

export module rstd.tests.std_leaf_modules;

import rstd.sys.libc;

#if RSTD_OS_UNIX || RSTD_OS_WINDOWS
import rstd.core;
import rstd.sys.mem;
#endif

export auto std_leaf_memory() -> bool {
    auto* memory = rstd::sys::libc::malloc(16);
    if (memory == nullptr) return false;
    rstd::sys::libc::memset(memory, 42, 16);
    auto const result = static_cast<unsigned char*>(memory)[15] == 42;
    rstd::sys::libc::free(memory);
    return result;
}

export auto std_leaf_mapping() -> bool {
#if RSTD_OS_UNIX || RSTD_OS_WINDOWS
    namespace backend = rstd::sys::mem;
    auto const info   = backend::information();
    if (info.page_size == rstd::usize() || info.mapping_alignment < info.page_size) return false;
    auto* memory = backend::map(info.page_size);
    if (memory == nullptr) return false;
    auto*      bytes  = static_cast<unsigned char*>(memory);
    auto const zeroed = bytes[0] == 0 && bytes[info.page_size.to_primitive() - 1] == 0;
    bytes[info.page_size.to_primitive() - 1] = 42;
    return backend::unmap(memory, info.page_size) && zeroed;
#else
    return true;
#endif
}

export auto std_leaf_platform() -> bool {
#if RSTD_OS_UNIX
    namespace libc = rstd::sys::libc;
    auto mutex     = libc::pthread_mutex_initializer();
    if (libc::pthread_mutex_lock(&mutex) != 0) return false;
    auto const unlocked  = libc::pthread_mutex_unlock(&mutex) == 0;
    auto const destroyed = libc::pthread_mutex_destroy(&mutex) == 0;
    if (! unlocked || ! destroyed) return false;
    if (libc::getpid() <= 0) return false;
#if RSTD_OS_LINUX
    auto const fd = libc::eventfd(0, libc::EFD_CLOEXEC);
    if (fd < 0) return false;
    return libc::close(fd) == 0;
#endif
#elif RSTD_OS_WINDOWS
    return rstd::sys::libc::GetCurrentProcessId() != 0;
#endif
    return true;
}
