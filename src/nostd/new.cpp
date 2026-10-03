#include <new>
#include <stdlib.h>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace
{

auto allocation_size(std::size_t size) noexcept -> std::size_t {
    return size == 0 ? 1 : size;
}

auto try_allocate_aligned(std::size_t size, std::align_val_t al) noexcept -> void* {
    auto alignment = static_cast<std::size_t>(al);
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) return nullptr;
    size = allocation_size(size);
#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    // POSIX requires pointer-sized alignment even for valid smaller C++ alignments.
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
#endif
}

} // namespace

void* operator new(std::size_t size) {
    void* ptr = malloc(allocation_size(size));
    if (! ptr) abort();
    return ptr;
}

void* operator new(std::size_t size, std::align_val_t al) {
    void* ptr = try_allocate_aligned(size, al);
    if (! ptr) abort();
    return ptr;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return malloc(allocation_size(size));
}

void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    return try_allocate_aligned(size, al);
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void* operator new[](std::size_t size, std::align_val_t al) {
    return ::operator new(size, al);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}

void* operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, al, tag);
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    free(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

void operator delete(void* ptr, std::size_t, std::align_val_t al) noexcept {
    ::operator delete(ptr, al);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    ::operator delete(ptr);
}

void operator delete(void* ptr, std::align_val_t al, const std::nothrow_t&) noexcept {
    ::operator delete(ptr, al);
}

void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept {
    ::operator delete(ptr, size);
}

void operator delete[](void* ptr, std::align_val_t al) noexcept {
    ::operator delete(ptr, al);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t al) noexcept {
    ::operator delete(ptr, size, al);
}

void operator delete[](void* ptr, const std::nothrow_t& tag) noexcept {
    ::operator delete(ptr, tag);
}

void operator delete[](void* ptr, std::align_val_t al, const std::nothrow_t& tag) noexcept {
    ::operator delete(ptr, al, tag);
}
