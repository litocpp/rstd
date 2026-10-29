#include <rstd/macro.hpp>
#include "../budget/objects.hpp"

import rstd;

using namespace rstd::prelude;

static bool     fail_next   = false;
static bool     fail_resize = false;
static unsigned allocations {}, zeroed_allocations {}, reallocations {}, deallocations {};
struct Request {
    rstd::size_t size {}, align {}, new_size {};
};
static Request last_allocation, last_zeroed, last_resize, last_release;

extern "C" void* __real___rstd_alloc(rstd::size_t size, rstd::size_t align);
extern "C" void* __real___rstd_alloc_zeroed(rstd::size_t size, rstd::size_t align);
extern "C" void* __real___rstd_realloc(void*, rstd::size_t, rstd::size_t, rstd::size_t);
extern "C" void  __real___rstd_dealloc(void*, rstd::size_t, rstd::size_t);

extern "C" void* __wrap___rstd_alloc(rstd::size_t size, rstd::size_t align) {
    ++allocations;
    last_allocation = { size, align };
    if (fail_next) {
        fail_next = false;
        return nullptr;
    }
    return __real___rstd_alloc(size, align);
}

extern "C" void* __wrap___rstd_alloc_zeroed(rstd::size_t size, rstd::size_t align) {
    ++zeroed_allocations;
    last_zeroed = { size, align };
    if (fail_next) {
        fail_next = false;
        return nullptr;
    }
    return __real___rstd_alloc_zeroed(size, align);
}

extern "C" void* __wrap___rstd_realloc(void*        pointer,
                                       rstd::size_t old_size,
                                       rstd::size_t align,
                                       rstd::size_t new_size) {
    ++reallocations;
    last_resize = { old_size, align, new_size };
    if (fail_resize) {
        fail_resize = false;
        return nullptr;
    }
    return __real___rstd_realloc(pointer, old_size, align, new_size);
}

extern "C" void __wrap___rstd_dealloc(void* pointer, rstd::size_t size, rstd::size_t align) {
    ++deallocations;
    last_release = { size, align };
    __real___rstd_dealloc(pointer, size, align);
}

static auto layout(unsigned size, unsigned alignment) -> rstd::alloc::Layout {
    return rstd::alloc::Layout::from_size_align(usize(size), usize(alignment)).unwrap();
}

static void global_resize_dispatch() {
    auto  allocator = as<rstd::alloc::Allocator>(::alloc::GLOBAL);
    auto  small     = layout(16, 1);
    auto  large     = layout(64, 1);
    auto* raw       = ::alloc::alloc(small);
    rstd_assert(last_allocation.align == 1 && last_allocation.size == 16);
    raw = ::alloc::realloc(raw, small, large.size);
    rstd_assert(raw && last_resize.align == 1 && last_resize.new_size == 64);
    ::alloc::dealloc(raw, large);
    rstd_assert(last_release.align == 1 && last_release.size == 64);
    raw = ::alloc::alloc_zeroed(small);
    rstd_assert(raw && last_zeroed.align == 1 && last_zeroed.size == 16);
    ::alloc::dealloc(raw, small);

    auto memory                                    = allocator.allocate(small).unwrap();
    static_cast<unsigned char*>(memory.pointer)[0] = 42;
    auto before_allocations                        = allocations;
    auto before_reallocations                      = reallocations;
    memory = allocator.grow_zeroed(memory.pointer, small, large).unwrap();
    rstd_assert(allocations == before_allocations && reallocations == before_reallocations + 1);
    rstd_assert(last_resize.size == 16 && last_resize.align == 1 && last_resize.new_size == 64);
    auto* bytes = static_cast<unsigned char*>(memory.pointer);
    rstd_assert(bytes[0] == 42);
    for (unsigned i = 16; i < 64; ++i) rstd_assert(bytes[i] == 0);

    fail_resize = true;
    rstd_assert(allocator.shrink(memory.pointer, large, small).is_err());
    rstd_assert(! fail_resize && bytes[0] == 42);
    auto larger = layout(128, 1);
    fail_resize = true;
    rstd_assert(allocator.grow(memory.pointer, large, larger).is_err());
    rstd_assert(! fail_resize && bytes[0] == 42);
    fail_resize = true;
    rstd_assert(allocator.grow_zeroed(memory.pointer, large, larger).is_err());
    rstd_assert(! fail_resize && bytes[0] == 42);
    memory = allocator.shrink(memory.pointer, large, small).unwrap();
    rstd_assert(last_resize.size == 64 && last_resize.new_size == 16);

    auto changed         = layout(64, 256);
    before_reallocations = reallocations;
    fail_next            = true;
    rstd_assert(allocator.grow(memory.pointer, small, changed).is_err());
    rstd_assert(! fail_next && reallocations == before_reallocations);
    rstd_assert(static_cast<unsigned char*>(memory.pointer)[0] == 42);
    fail_next = true;
    rstd_assert(allocator.grow_zeroed(memory.pointer, small, changed).is_err());
    rstd_assert(! fail_next && reallocations == before_reallocations);
    rstd_assert(static_cast<unsigned char*>(memory.pointer)[0] == 42);
    memory = allocator.grow(memory.pointer, small, changed).unwrap();
    rstd_assert(last_allocation.align == 256 && last_release.align == 1);
    rstd_assert(reinterpret_cast<rstd::uintptr_t>(memory.pointer) % 256 == 0);
    fail_next = true;
    rstd_assert(allocator.shrink(memory.pointer, changed, layout(8, 4096)).is_err());
    rstd_assert(! fail_next && reallocations == before_reallocations);
    rstd_assert(static_cast<unsigned char*>(memory.pointer)[0] == 42);
    allocator.deallocate(memory.pointer, changed);
    rstd_assert(last_release.align == 256);

    auto empty_layout = layout(0, 64);
    auto before       = allocations + zeroed_allocations + reallocations + deallocations;
    auto empty        = allocator.allocate(empty_layout).unwrap();
    empty             = allocator.grow_zeroed(empty.pointer, empty_layout, empty_layout).unwrap();
    allocator.deallocate(empty.pointer, empty_layout);
    rstd_assert(before == allocations + zeroed_allocations + reallocations + deallocations);
    fail_next = true;
    rstd_assert(allocator.grow(empty.pointer, empty_layout, small).is_err());
    rstd_assert(! fail_next);
    before_reallocations = reallocations;
    memory               = allocator.grow_zeroed(empty.pointer, empty_layout, small).unwrap();
    rstd_assert(reallocations == before_reallocations);
    empty = allocator.shrink(memory.pointer, small, empty_layout).unwrap();
    rstd_assert(empty.pointer == empty_layout.dangling());
    allocator.deallocate(empty.pointer, empty_layout);

    auto values = Vec<u8>::with_capacity(usize(1));
    rstd_assert(last_allocation.align == 1);
    values.push(u8(42));
    before_reallocations = reallocations;
    values.reserve(usize(100));
    rstd_assert(reallocations == before_reallocations + 1 && last_resize.align == 1);
    rstd_assert(values[usize()].get() == u8(42));
    fail_resize = true;
    rstd_assert(values.try_reserve(usize(1000)).is_err());
    rstd_assert(! fail_resize && values.len() == usize(1) && values[usize()].get() == u8(42));
}

template<typename Owner>
void sized_failure() {
    Counts counts;
    fail_next = true;
    rstd_assert(Owner::try_make(counts, 42).is_err());
    rstd_assert(! fail_next);
    rstd_assert(counts.constructed == 0 && counts.dropped == 0);
    {
        auto owner = Owner::try_make(counts, 42).unwrap();
        rstd_assert(owner->value == 42);
    }
    rstd_assert(counts.constructed == 1 && counts.dropped == 1);
}

template<typename Owner>
void dyn_failure() {
    Counts counts;
    {
        Tracked input(counts, 42);
        fail_next = true;
        rstd_assert(Owner::try_make(rstd::move(input)).is_err());
        rstd_assert(! fail_next);
        rstd_assert(input.counts == &counts && input.value == 42);
        rstd_assert(counts.moved == 0 && counts.dropped == 0);
        auto owner = Owner::try_make(rstd::move(input)).unwrap();
        rstd_assert(counts.moved == 1 && input.counts == nullptr);
    }
    rstd_assert(counts.dropped == 1);
}

#include "../budget/pool.hpp"

template<typename F>
void capacity_failure(F factory) {
    fail_next   = true;
    auto result = factory();
    rstd_assert(result.is_err());
    rstd_assert(result.unwrap_err() == ::alloc::collections::TryReserveError::AllocError);
    rstd_assert(! fail_next);
}

void capacity_failures() {
    using Error = ::alloc::collections::TryReserveError;
    using rstd::bytes::Bytes;
    using rstd::bytes::BytesMut;
    capacity_failure([] {
        return Vec<u8>::try_with_capacity(usize(16));
    });
    capacity_failure([] {
        return String::try_with_capacity(usize(16));
    });
    capacity_failure([] {
        return BytesMut::try_with_capacity(usize(16));
    });
    capacity_failure([] {
        return rstd::collections::HashTable<u32>::try_with_capacity(usize(8));
    });
    capacity_failure([] {
        return rstd::collections::HashMap<u32, u32>::try_with_capacity(usize(8));
    });
    capacity_failure([] {
        return rstd::collections::HashMap<u32, u32>::try_with_capacity_and_hasher(
            usize(8), rstd::hash::RandomState {});
    });
    capacity_failure([] {
        return rstd::collections::HashSet<u32>::try_with_capacity(usize(8));
    });
    fail_next = true;
    rstd_assert(Bytes::try_make_in(::alloc::Global {}).is_err());
    rstd_assert(! fail_next);
    fail_next = true;
    rstd_assert(BytesMut::try_make_in(::alloc::Global {}).is_err());
    rstd_assert(! fail_next);
    fail_next = true;
    rstd_assert(Vec<u8>::try_with_capacity(usize::MAX).unwrap_err() == Error::CapacityOverflow);
    rstd_assert(String::try_with_capacity(usize::MAX).unwrap_err() == Error::CapacityOverflow);
    rstd_assert(BytesMut::try_with_capacity(usize()).unwrap().is_empty());
    rstd_assert(fail_next);
    fail_next = false;
    auto text = String::with_capacity(usize(8));
    text.push_ascii('x');
    auto pointer  = text.data();
    auto capacity = text.capacity();
    fail_resize   = true;
    rstd_assert(text.try_reserve_exact(usize(64)).unwrap_err() == Error::AllocError);
    rstd_assert(! fail_resize);
    rstd_assert(text.len() == usize(1) && text.data() == pointer && text.capacity() == capacity);
    rstd_assert(text.as_str().as_bytes()[usize()] == u8('x'));
    BytesMut empty;
    fail_next = true;
    rstd_assert(empty.try_reserve(usize(64)).unwrap_err() == Error::AllocError);
    rstd_assert(! fail_next && empty.data() == nullptr && empty.capacity() == usize());
    auto buffer = BytesMut::with_capacity(usize(8));
    fail_resize = true;
    rstd_assert(buffer.try_reserve_exact(usize(64)).unwrap_err() == Error::AllocError);
    rstd_assert(! fail_resize && buffer.capacity() == usize(8) && buffer.is_empty());
}

int main() {
    auto before_pool = allocations + zeroed_allocations + reallocations;
    fail_next        = true;
    pool_lifetime();
    rstd_assert(fail_next);
    fail_next = false;
    rstd_assert(allocations + zeroed_allocations + reallocations == before_pool);
    sized_failure<Box<Tracked>>();
    sized_failure<rstd::sync::Arc<Tracked>>();
    dyn_failure<Box<dyn<rstd::any::Any>>>();
    dyn_failure<rstd::sync::Arc<dyn<rstd::any::Any>>>();
    fail_next = true;
    rstd_assert(rstd::alloc::BudgetAllocator<>::try_make(usize(64)).is_err());
    rstd_assert(! fail_next);
    auto budget = rstd::alloc::BudgetAllocator<>::try_make(usize(64)).unwrap();
    auto layout = rstd::alloc::Layout::make<Tracked>();
    fail_next   = true;
    rstd_assert(as<rstd::alloc::Allocator>(budget).allocate(layout).is_err());
    rstd_assert(! fail_next);
    auto stats = budget.statistics();
    rstd_assert(stats.live_bytes == usize() && stats.peak_bytes == usize());
    rstd_assert(stats.allocations == usize() && stats.failures == usize(1));
    auto memory = as<rstd::alloc::Allocator>(budget).allocate(layout).unwrap();
    rstd_assert(budget.statistics().live_bytes == usize(64));
    as<rstd::alloc::Allocator>(budget).deallocate(memory.pointer, layout);
    rstd_assert(budget.statistics().live_bytes == usize());
    global_resize_dispatch();
    capacity_failures();
}
