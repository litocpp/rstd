export module rstd.tests.alloc_module;
import rstd.alloc;

using namespace rstd::prelude;
using rstd::alloc::Allocator;
using rstd::alloc::Layout;

struct GlobalDynAllocation {
    ref<dyn<Allocator>> allocator = ::alloc::allocator_ref(::alloc::GLOBAL);
    Layout              layout;
    void*               pointer;

    ~GlobalDynAllocation() { allocator->deallocate(pointer, layout); }

    auto aligned() const -> bool {
        return reinterpret_cast<rstd::uintptr_t>(pointer) % layout.align.to_primitive() == 0;
    }

    auto bytes() const -> unsigned char* { return static_cast<unsigned char*>(pointer); }
};

export auto global_dyn_resize() -> bool {
    auto allocator = ::alloc::allocator_ref(::alloc::GLOBAL);
    auto initial   = Layout::from_size_align(usize(8), usize(64)).unwrap();
    auto result    = allocator->allocate(initial);
    if (result.is_err()) return false;
    GlobalDynAllocation memory { allocator, initial, result.unwrap().pointer };
    if (! memory.aligned()) return false;
    for (unsigned i = 0; i < 8; ++i) memory.bytes()[i] = 0x5a;

    auto larger = Layout::from_size_align(usize(16), usize(128)).unwrap();
    auto grown  = allocator->grow(memory.pointer, memory.layout, larger);
    if (grown.is_err()) return false;
    memory.pointer = grown.unwrap().pointer;
    memory.layout  = larger;
    if (! memory.aligned()) return false;
    for (unsigned i = 0; i < 8; ++i)
        if (memory.bytes()[i] != 0x5a) return false;
    for (unsigned i = 8; i < 16; ++i) memory.bytes()[i] = 0xa5;

    auto zeroed_layout = Layout::from_size_align(usize(32), usize(256)).unwrap();
    auto zeroed        = allocator->grow_zeroed(memory.pointer, memory.layout, zeroed_layout);
    if (zeroed.is_err()) return false;
    memory.pointer = zeroed.unwrap().pointer;
    memory.layout  = zeroed_layout;
    if (! memory.aligned()) return false;
    for (unsigned i = 0; i < 32; ++i) {
        auto expected = i < 8 ? 0x5a : i < 16 ? 0xa5 : 0;
        if (memory.bytes()[i] != expected) return false;
    }

    auto smaller = Layout::from_size_align(usize(4), usize(512)).unwrap();
    auto shrunk  = allocator->shrink(memory.pointer, memory.layout, smaller);
    if (shrunk.is_err()) return false;
    memory.pointer = shrunk.unwrap().pointer;
    memory.layout  = smaller;
    if (! memory.aligned()) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (memory.bytes()[i] != 0x5a) return false;
    return true;
}

export auto global_dyn_zeroed_and_empty() -> bool {
    auto allocator = ::alloc::allocator_ref(::alloc::GLOBAL);
    auto layout    = Layout::from_size_align(usize(16), usize(64)).unwrap();
    auto result    = allocator->allocate_zeroed(layout);
    if (result.is_err()) return false;
    GlobalDynAllocation memory { allocator, layout, result.unwrap().pointer };
    if (! memory.aligned()) return false;
    for (unsigned i = 0; i < 16; ++i)
        if (memory.bytes()[i] != 0) return false;

    auto empty  = Layout::from_size_align(usize(), usize(64)).unwrap();
    auto shrunk = allocator->shrink(memory.pointer, memory.layout, empty);
    if (shrunk.is_err()) return false;
    memory.pointer = shrunk.unwrap().pointer;
    memory.layout  = empty;
    if (! memory.aligned() || memory.pointer == nullptr) return false;
    auto grown = allocator->grow_zeroed(memory.pointer, memory.layout, layout);
    if (grown.is_err()) return false;
    memory.pointer = grown.unwrap().pointer;
    memory.layout  = layout;
    if (! memory.aligned()) return false;
    for (unsigned i = 0; i < 16; ++i)
        if (memory.bytes()[i] != 0) return false;

    auto allocated = allocator->allocate(empty);
    if (allocated.is_err()) return false;
    GlobalDynAllocation empty_memory { allocator, empty, allocated.unwrap().pointer };
    return empty_memory.pointer != nullptr && empty_memory.aligned();
}
