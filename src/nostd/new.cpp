#include <new>
#include <stdlib.h>
#include <stdio.h>

void* operator new(std::size_t size) {
    void* ptr = malloc(size);
    if (! ptr && size > 0) abort();
    return ptr;
}

void* operator new(std::size_t size, std::align_val_t al) {
#ifdef _WIN32
    void* ptr = _aligned_malloc(size, static_cast<std::size_t>(al));
    if (! ptr) abort();
    return ptr;
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, static_cast<std::size_t>(al), size) != 0) {
        abort();
    }
    return ptr;
#endif
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return malloc(size); }

void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
#ifdef _WIN32
    return _aligned_malloc(size, static_cast<std::size_t>(al));
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, static_cast<std::size_t>(al), size) != 0) {
        return nullptr;
    }
    return ptr;
#endif
}

void operator delete(void* ptr) noexcept { free(ptr); }

void operator delete(void* ptr, std::size_t) noexcept { free(ptr); }

void operator delete(void* ptr, std::align_val_t) noexcept { free(ptr); }

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept { free(ptr); }
