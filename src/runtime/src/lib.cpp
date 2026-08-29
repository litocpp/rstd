module;
#include <rstd/macro.hpp>
#if ! RSTD_ARCH_WASM || ! RSTD_OS_UNKNOWN
#include <new>
#include <stdio.h>
#include <stdlib.h>
#endif

module rstd.runtime;

using namespace rstd;

extern "C" {

#if RSTD_ARCH_WASM && RSTD_OS_UNKNOWN

#if defined(__wasm_atomics__) || defined(__wasm_shared_memory__)
#error "rstd wasm runtime does not support shared memory or atomics on unknown OS targets"
#endif

extern unsigned char __heap_base;

struct FreeBlock {
    rstd::size_t size;
    FreeBlock*   next;
};

struct AllocationHeader {
    unsigned char* block;
    rstd::size_t   size;
};

FreeBlock*     free_blocks;
unsigned char* heap_cursor;

constexpr rstd::size_t WASM_PAGE_SIZE = 65536;

auto checked_add(rstd::size_t left, rstd::size_t right, rstd::size_t& result) noexcept -> bool {
    result = left + right;
    return result >= left;
}

auto align_up(rstd::size_t value, rstd::size_t alignment, rstd::size_t& result) noexcept -> bool {
    auto mask = alignment - 1;
    if ((alignment & mask) != 0) return false;
    rstd::size_t adjusted;
    if (! checked_add(value, mask, adjusted)) return false;
    result = adjusted & ~mask;
    return true;
}

auto memory_end() noexcept -> unsigned char* {
    auto pages = static_cast<rstd::size_t>(__builtin_wasm_memory_size(0));
    return reinterpret_cast<unsigned char*>(pages * WASM_PAGE_SIZE);
}

auto grow_memory(unsigned char* required) noexcept -> bool {
    auto current = memory_end();
    if (required <= current) return true;
    auto         missing = static_cast<rstd::size_t>(required - current);
    rstd::size_t rounded;
    if (! checked_add(missing, WASM_PAGE_SIZE - 1, rounded)) return false;
    auto pages = rounded / WASM_PAGE_SIZE;
    return __builtin_wasm_memory_grow(0, pages) != static_cast<rstd::size_t>(-1);
}

auto allocation_from(unsigned char* block,
                     rstd::size_t   block_size,
                     rstd::size_t   size,
                     rstd::size_t   alignment) noexcept -> void* {
    rstd::size_t payload_address;
    auto         block_address = reinterpret_cast<rstd::size_t>(block);
    if (! checked_add(block_address, sizeof(AllocationHeader), payload_address) ||
        ! align_up(payload_address, alignment, payload_address)) {
        return nullptr;
    }
    rstd::size_t allocation_end;
    rstd::size_t block_end;
    if (! checked_add(payload_address, size, allocation_end) ||
        ! checked_add(block_address, block_size, block_end) || allocation_end > block_end) {
        return nullptr;
    }
    auto* header  = reinterpret_cast<AllocationHeader*>(payload_address) - 1;
    header->block = block;
    header->size  = block_size;
    return reinterpret_cast<void*>(payload_address);
}

auto take_free_block(rstd::size_t size, rstd::size_t alignment) noexcept -> void* {
    FreeBlock** link = &free_blocks;
    while (*link != nullptr) {
        auto* block      = *link;
        auto* next       = block->next;
        auto  block_size = block->size;
        auto* result =
            allocation_from(reinterpret_cast<unsigned char*>(block), block_size, size, alignment);
        if (result != nullptr) {
            *link = next;
            return result;
        }
        link = &block->next;
    }
    return nullptr;
}

auto allocate_fresh(rstd::size_t size, rstd::size_t alignment) noexcept -> void* {
    if (heap_cursor == nullptr) heap_cursor = &__heap_base;
    auto         block = heap_cursor;
    rstd::size_t payload_address;
    auto         block_address = reinterpret_cast<rstd::size_t>(block);
    if (! checked_add(block_address, sizeof(AllocationHeader), payload_address) ||
        ! align_up(payload_address, alignment, payload_address)) {
        return nullptr;
    }
    rstd::size_t end_address;
    if (! checked_add(payload_address, size, end_address)) return nullptr;
    rstd::size_t aligned_end;
    if (! align_up(end_address, alignof(FreeBlock), aligned_end)) return nullptr;
    auto* required = reinterpret_cast<unsigned char*>(aligned_end);
    if (! grow_memory(required)) return nullptr;
    heap_cursor = required;
    return allocation_from(block, aligned_end - block_address, size, alignment);
}

void insert_free_block(unsigned char* address, rstd::size_t size) noexcept {
    auto* block = reinterpret_cast<FreeBlock*>(address);
    block->size = size;
    auto** link = &free_blocks;
    while (*link != nullptr && *link < block) link = &(*link)->next;
    block->next = *link;
    *link       = block;
    if (block->next != nullptr && reinterpret_cast<unsigned char*>(block) + block->size ==
                                      reinterpret_cast<unsigned char*>(block->next)) {
        block->size += block->next->size;
        block->next = block->next->next;
    }
    if (link != &free_blocks) {
        auto* previous = free_blocks;
        while (previous->next != block) previous = previous->next;
        if (reinterpret_cast<unsigned char*>(previous) + previous->size ==
            reinterpret_cast<unsigned char*>(block)) {
            previous->size += block->size;
            previous->next = block->next;
        }
    }
}

[[clang::import_module("rstd"), clang::import_name("panic")]]
void rstd_host_panic(const rstd::uint8_t* data, rstd::size_t size);

#define RSTD_WASM_EXPORT(name) [[clang::export_name(name)]]

__attribute__((optnone)) void*
memcpy(void* destination, const void* source, rstd::size_t size) noexcept {
    auto*       output = static_cast<unsigned char*>(destination);
    const auto* input  = static_cast<const unsigned char*>(source);
    for (rstd::size_t index = 0; index < size; ++index) output[index] = input[index];
    return destination;
}

__attribute__((optnone)) void*
memmove(void* destination, const void* source, rstd::size_t size) noexcept {
    auto*       output = static_cast<unsigned char*>(destination);
    const auto* input  = static_cast<const unsigned char*>(source);
    if (output < input) {
        for (rstd::size_t index = 0; index < size; ++index) output[index] = input[index];
    } else if (output > input) {
        for (rstd::size_t index = size; index != 0; --index) output[index - 1] = input[index - 1];
    }
    return destination;
}

__attribute__((optnone)) void* memset(void* destination, int value, rstd::size_t size) noexcept {
    auto* output = static_cast<unsigned char*>(destination);
    for (rstd::size_t index = 0; index < size; ++index) {
        output[index] = static_cast<unsigned char>(value);
    }
    return destination;
}

__attribute__((optnone)) int
memcmp(const void* left, const void* right, rstd::size_t size) noexcept {
    const auto* left_bytes  = static_cast<const unsigned char*>(left);
    const auto* right_bytes = static_cast<const unsigned char*>(right);
    for (rstd::size_t index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return static_cast<int>(left_bytes[index]) - static_cast<int>(right_bytes[index]);
        }
    }
    return 0;
}

#else

#define RSTD_WASM_EXPORT(name)

#endif

RSTD_WASM_EXPORT("__rstd_alloc")
void* __rstd_alloc(rstd::size_t size, rstd::size_t align) {
#if RSTD_ARCH_WASM && RSTD_OS_UNKNOWN
    if (size == 0) size = 1;
    if (align < alignof(AllocationHeader)) align = alignof(AllocationHeader);
    auto* reused = take_free_block(size, align);
    return reused != nullptr ? reused : allocate_fresh(size, align);
#else
    void* result;
    if (align > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        result = ::operator new(size, std::align_val_t { align }, std::nothrow_t {});
    } else {
        result = ::operator new(size, std::nothrow_t {});
    }
    return result;
#endif
}

RSTD_WASM_EXPORT("__rstd_dealloc")
void __rstd_dealloc(void* ptr, rstd::size_t size, rstd::size_t align) {
#if RSTD_ARCH_WASM && RSTD_OS_UNKNOWN
    (void)size;
    (void)align;
    if (ptr == nullptr) return;
    auto* header = static_cast<AllocationHeader*>(ptr) - 1;
    insert_free_block(header->block, header->size);
#else
    if (align > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        ::operator delete(ptr, size, std::align_val_t { align });
    } else {
        ::operator delete(ptr, size);
    }
#endif
}

RSTD_WASM_EXPORT("__rstd_realloc")
void* __rstd_realloc(void* ptr, rstd::size_t old_size, rstd::size_t align, rstd::size_t new_size) {
    if (ptr == nullptr) return __rstd_alloc(new_size, align);
    void* new_ptr = __rstd_alloc(new_size, align);
    if (new_ptr) {
        __builtin_memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
        __rstd_dealloc(ptr, old_size, align);
    }
    return new_ptr;
}

RSTD_WASM_EXPORT("__rstd_alloc_zeroed")
void* __rstd_alloc_zeroed(rstd::size_t size, rstd::size_t align) {
    void* ptr = __rstd_alloc(size, align);
    if (ptr) {
        __builtin_memset(ptr, 0, size);
    }
    return ptr;
}

[[noreturn]]
void rstd_panic_impl(rstd::panic_::PanicInfo const& info) {
#if RSTD_ARCH_WASM && RSTD_OS_UNKNOWN
    constexpr char prefix[] = "rstd panic: ";
    rstd_host_panic(reinterpret_cast<const rstd::uint8_t*>(prefix), sizeof(prefix) - 1);
    if (info.fmt != nullptr) {
        info.fmt(
            info.data, nullptr, +[](void*, const rstd::uint8_t* data, rstd::size_t size) -> bool {
                rstd_host_panic(data, size);
                return true;
            });
    }
    __builtin_trap();
#else
    auto& loc = info.location;

    fprintf(
        stderr, "thread 'main' panicked at %s:%u:%u:\n", loc.file_name(), loc.line(), loc.column());

    if (info.fmt) {
        FILE* f = stderr;
        info.fmt(
            info.data, &f, +[](void* ctx, rstd::uint8_t const* buf, rstd::size_t len) -> bool {
                return fwrite(buf, 1, len, *static_cast<FILE**>(ctx)) == len;
            });
    }

    fputc('\n', stderr);
    fflush(stderr);
    abort();
#endif
}
}
