#include <new>
#include <stdint.h>

int test_aligned_new() {
    const std::nothrow_t tag {};
    const std::size_t    alignments[] = { 1, 2, 4, 8, 16, 64, 4096 };
    const std::size_t    sizes[]      = { 0, 1, 264 };
    for (std::size_t alignment : alignments) {
        auto al = static_cast<std::align_val_t>(alignment);
        for (std::size_t size : sizes) {
            void* pointers[] = { ::operator new(size, al),
                                 ::operator new(size, al, tag),
                                 ::operator new[](size, al),
                                 ::operator new[](size, al, tag) };
            for (void* ptr : pointers) {
                if (! ptr || reinterpret_cast<uintptr_t>(ptr) % alignment != 0) return 4;
            }
            ::operator delete(pointers[0], al);
            ::operator delete(pointers[1], al, tag);
            ::operator delete[](pointers[2], al);
            ::operator delete[](pointers[3], al, tag);
            ::operator delete(::operator new(size, al), size, al);
            ::operator delete[](::operator new[](size, al), size, al);
        }
    }
    const std::size_t invalid_alignments[] = { 0, 3, 6 };
    for (std::size_t alignment : invalid_alignments) {
        auto al = static_cast<std::align_val_t>(alignment);
        if (::operator new(264, al, tag) != nullptr) return 5;
        if (::operator new[](264, al, tag) != nullptr) return 6;
    }
    return 0;
}
