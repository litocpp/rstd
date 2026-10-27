import rstd.alloc;

using namespace rstd::prelude;
using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using ::alloc::vec::Vec;

static auto layout(rstd::size_t size, rstd::size_t align) -> Layout {
    return Layout::from_size_align(usize(size), usize(align)).unwrap();
}
static auto aligned(void* pointer, rstd::size_t alignment) -> bool {
    return pointer && reinterpret_cast<rstd::uintptr_t>(pointer) % alignment == 0;
}

#if defined(__wasm__)
#define RSTD_CHECK(name) extern "C" [[clang::export_name(name)]]
#else
#define RSTD_CHECK(name) extern "C"
#endif

RSTD_CHECK("rstd_global_layouts")
auto rstd_global_layouts() -> int {
    constexpr rstd::size_t alignments[] { 1, 2, 4, 8, 16, 64, 4096 };
    auto                   allocator = as<Allocator>(::alloc::GLOBAL);
    for (auto alignment : alignments) {
        auto original = layout(37, alignment);
        auto memory   = allocator.allocate_zeroed(original).unwrap();
        if (! aligned(memory.pointer, alignment)) return 1;
        auto* bytes = static_cast<unsigned char*>(memory.pointer);
        for (unsigned i {}; i < 37; ++i) {
            if (bytes[i]) return 2;
            bytes[i] = static_cast<unsigned char>(i + 1);
        }
        auto larger = layout(4099, alignment);
        memory      = allocator.grow_zeroed(memory.pointer, original, larger).unwrap();
        if (! aligned(memory.pointer, alignment) || memory.size != larger.size) return 3;
        bytes = static_cast<unsigned char*>(memory.pointer);
        for (unsigned i {}; i < 4099; ++i)
            if (bytes[i] != (i < 37 ? i + 1 : 0)) return 4;
        auto smaller = layout(13, alignment);
        memory       = allocator.shrink(memory.pointer, larger, smaller).unwrap();
        if (! aligned(memory.pointer, alignment) || memory.size != smaller.size) return 5;
        bytes = static_cast<unsigned char*>(memory.pointer);
        for (unsigned i {}; i < 13; ++i)
            if (bytes[i] != i + 1) return 6;
        auto same = allocator.grow(memory.pointer, smaller, smaller).unwrap();
        if (same.pointer != memory.pointer) return 7;
        same = allocator.shrink(memory.pointer, smaller, smaller).unwrap();
        if (same.pointer != memory.pointer) return 8;
        allocator.deallocate(memory.pointer, smaller);

        auto* raw = static_cast<unsigned char*>(::alloc::alloc(original));
        if (! aligned(raw, alignment)) return 9;
        raw[0] = 42;
        raw    = static_cast<unsigned char*>(::alloc::realloc(raw, original, larger.size));
        if (! aligned(raw, alignment) || raw[0] != 42) return 10;
        ::alloc::dealloc(raw, larger);
    }
    return 0;
}

RSTD_CHECK("rstd_global_zero_sized")
auto rstd_global_zero_sized() -> int {
    auto allocator  = as<Allocator>(::alloc::GLOBAL);
    auto zero       = layout(0, 64);
    auto other_zero = layout(0, 256);
    auto small      = layout(51, 1);
    auto empty      = allocator.allocate_zeroed(zero).unwrap();
    if (empty.pointer != zero.dangling() || empty.size != usize()) return 1;
    empty = allocator.grow(empty.pointer, zero, other_zero).unwrap();
    if (empty.pointer != other_zero.dangling()) return 2;
    auto memory = allocator.grow_zeroed(empty.pointer, other_zero, small).unwrap();
    if (! aligned(memory.pointer, 1)) return 3;
    auto* bytes = static_cast<unsigned char*>(memory.pointer);
    for (unsigned i {}; i < 51; ++i)
        if (bytes[i]) return 4;
    empty = allocator.shrink(memory.pointer, small, zero).unwrap();
    if (empty.pointer != zero.dangling()) return 5;
    empty = allocator.shrink(empty.pointer, zero, other_zero).unwrap();
    allocator.deallocate(empty.pointer, other_zero);
    memory = allocator.grow(empty.pointer, other_zero, small).unwrap();
    allocator.deallocate(memory.pointer, small);
    return 0;
}

RSTD_CHECK("rstd_global_dyn_resize")
auto rstd_global_dyn_resize() -> int {
    auto  allocator = ::alloc::allocator_ref(::alloc::GLOBAL);
    auto  original  = layout(17, 1);
    auto  memory    = allocator->allocate(original).unwrap();
    auto* bytes     = static_cast<unsigned char*>(memory.pointer);
    for (unsigned i {}; i < 17; ++i) bytes[i] = 42;
    auto grown = layout(83, 256);
    memory     = allocator->grow_zeroed(memory.pointer, original, grown).unwrap();
    if (! aligned(memory.pointer, 256)) return 1;
    bytes = static_cast<unsigned char*>(memory.pointer);
    for (unsigned i {}; i < 83; ++i)
        if (bytes[i] != (i < 17 ? 42 : 0)) return 2;
    auto smaller = layout(9, 4096);
    memory       = allocator->shrink(memory.pointer, grown, smaller).unwrap();
    if (! aligned(memory.pointer, 4096)) return 3;
    bytes = static_cast<unsigned char*>(memory.pointer);
    for (unsigned i {}; i < 9; ++i)
        if (bytes[i] != 42) return 4;
    auto same_size = layout(9, 2);
    memory         = allocator->grow(memory.pointer, smaller, same_size).unwrap();
    if (! aligned(memory.pointer, 2)) return 5;
    bytes = static_cast<unsigned char*>(memory.pointer);
    for (unsigned i {}; i < 9; ++i)
        if (bytes[i] != 42) return 6;
    allocator->deallocate(memory.pointer, same_size);
    return 0;
}

RSTD_CHECK("rstd_global_failure")
auto rstd_global_failure() -> int {
    auto                   allocator = as<Allocator>(::alloc::GLOBAL);
    constexpr rstd::size_t alignments[] { 1, 256 };
    for (auto alignment : alignments) {
        auto  original = layout(64, alignment);
        auto  memory   = allocator.allocate(original).unwrap();
        auto* bytes    = static_cast<unsigned char*>(memory.pointer);
        for (unsigned i {}; i < 64; ++i) bytes[i] = static_cast<unsigned char>(i + 1);
        auto huge =
            Layout::from_size_align(usize::MAX - usize(alignment - 1), usize(alignment)).unwrap();
        if (allocator.grow(memory.pointer, original, huge).is_ok()) return 1;
        if (allocator.grow_zeroed(memory.pointer, original, huge).is_ok()) return 2;
        for (unsigned i {}; i < 64; ++i)
            if (bytes[i] != i + 1) return 3;
        allocator.deallocate(memory.pointer, original);
    }
    return 0;
}

struct Tracked {
    unsigned* drops;
    unsigned  value;
    Tracked(unsigned& drops, unsigned value): drops(&drops), value(value) {}
    Tracked(const Tracked&) = delete;
    Tracked(Tracked&& other) noexcept: drops(other.drops), value(other.value) {
        other.drops = nullptr;
    }
    ~Tracked() {
        if (drops) ++*drops;
    }
};
struct alignas(256) Aligned {
    unsigned value;
};

RSTD_CHECK("rstd_global_collections")
auto rstd_global_collections() -> int {
    auto bytes = Vec<u8>::with_capacity(usize(1));
    for (unsigned i {}; i < 1024; ++i) bytes.push(u8(i % 251));
    for (unsigned i {}; i < 1024; ++i)
        if (bytes[usize(i)].get().to_primitive() != i % 251) return 1;
    bytes.truncate(usize(19));
    bytes.shrink_to_fit();
    for (unsigned i {}; i < 19; ++i)
        if (bytes[usize(i)].get().to_primitive() != i % 251) return 2;
    auto values = Vec<Aligned>::with_capacity(usize(1));
    for (unsigned i {}; i < 64; ++i) values.push(Aligned { i });
    if (reinterpret_cast<rstd::uintptr_t>(values.as_slice().as_raw_ptr()) % 256) return 3;
    for (unsigned i {}; i < 64; ++i)
        if (values[usize(i)].value != i) return 4;
    unsigned drops {};
    {
        auto tracked = Vec<Tracked>::with_capacity(usize(1));
        for (unsigned i {}; i < 128; ++i) tracked.push(Tracked(drops, i));
        for (unsigned i {}; i < 128; ++i)
            if (tracked[usize(i)].value != i) return 5;
        tracked.shrink_to_fit();
        if (drops) return 6;
    }
    if (drops != 128) return 7;
    ::alloc::string::String text;
    for (unsigned i {}; i < 1024; ++i) text.push_ascii(static_cast<char>('a' + i % 26));
    if (text.len() != usize(1024)) return 8;
    for (unsigned i {}; i < 1024; ++i)
        if (text.as_str()->as_bytes()[usize(i)].to_primitive() != 'a' + i % 26) return 9;
    return 0;
}

#if ! defined(__wasm__)
auto main() -> int {
    int (*checks[])() { rstd_global_layouts,
                        rstd_global_zero_sized,
                        rstd_global_dyn_resize,
                        rstd_global_failure,
                        rstd_global_collections };
    for (auto check : checks) {
        auto result = check();
        if (result) return result;
    }
    return 0;
}
#endif
