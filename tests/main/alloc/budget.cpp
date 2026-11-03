#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;

namespace
{

struct Counts {
    int constructed = 0;
    int moved       = 0;
    int dropped     = 0;
};

struct alignas(64) Tracked {
    Counts* counts;
    int     value;

    Tracked(Counts& counts, int value): counts(&counts), value(value) { ++counts.constructed; }
    Tracked(const Tracked&) = delete;
    Tracked(Tracked&& other) noexcept: counts(other.counts), value(other.value) {
        ++counts->moved;
        other.counts = nullptr;
    }
    ~Tracked() {
        if (counts) ++counts->dropped;
    }
};

using namespace rstd::prelude;
using rstd::alloc::Allocator;
using rstd::alloc::BudgetAllocator;
using rstd::alloc::Layout;

template<typename A>
void exercise(A budget) {
    auto small  = Layout::array<u8>(usize(64)).unwrap();
    auto large  = Layout::array<u8>(usize(128)).unwrap();
    auto api    = rstd::as<Allocator>(budget);
    auto memory = api.allocate_zeroed(small).unwrap();
    for (int i = 0; i < 64; ++i) rstd_assert(static_cast<rstd::uint8_t*>(memory.pointer)[i] == 0);
    static_cast<rstd::uint8_t*>(memory.pointer)[0] = 42;
    rstd_assert(api.grow(memory.pointer, small, large).is_err());
    rstd_assert(budget.statistics().live_bytes == usize(64));
    rstd_assert(static_cast<rstd::uint8_t*>(memory.pointer)[0] == 42);
    auto handle = rstd::thread::builder::Builder::make()
                      .spawn([owner = budget.clone(), memory, small] {
                          rstd::as<Allocator>(owner).deallocate(memory.pointer, small);
                      })
                      .unwrap();
    rstd::move(handle).join().unwrap();
    rstd_assert(budget.statistics().live_bytes == usize());
    rstd_assert(budget.statistics().peak_bytes == usize(64));
    rstd_assert(budget.statistics().failures == usize(1));
}

void resize_and_clone() {
    exercise(BudgetAllocator<>::try_make(usize(128)).unwrap());
    exercise(BudgetAllocator<rstd::alloc::VirtualMemoryAllocator>::try_make(usize(128)).unwrap());
    auto budget = BudgetAllocator<>::try_make(usize(192)).unwrap();
    auto first  = Layout::array<u8>(usize(64)).unwrap();
    auto second = Layout::array<u8>(usize(128)).unwrap();
    auto api    = rstd::as<Allocator>(budget);
    auto memory = api.allocate_zeroed(first).unwrap();
    static_cast<rstd::uint8_t*>(memory.pointer)[0] = 42;
    auto grown = api.grow_zeroed(memory.pointer, first, second).unwrap();
    rstd_assert(budget.statistics().peak_bytes == usize(192));
    rstd_assert(budget.statistics().live_bytes == usize(128));
    rstd_assert(static_cast<rstd::uint8_t*>(grown.pointer)[0] == 42);
    for (int i = 64; i < 128; ++i) rstd_assert(static_cast<rstd::uint8_t*>(grown.pointer)[i] == 0);
    auto shrunk = api.shrink(grown.pointer, second, first).unwrap();
    rstd_assert(static_cast<rstd::uint8_t*>(shrunk.pointer)[0] == 42);
    rstd_assert(budget.statistics().live_bytes == usize(64));
    auto clone    = as<Clone>(budget).clone();
    auto replaced = BudgetAllocator<>::try_make(usize(1)).unwrap();
    as<Clone>(replaced).clone_from(clone);
    as<Allocator>(replaced).deallocate(shrunk.pointer, first);
    rstd_assert(budget.statistics().live_bytes == usize());
    rstd_assert(replaced.statistics().allocations == usize(3));

    auto tight                                   = BudgetAllocator<>::try_make(usize(128)).unwrap();
    auto tight_api                               = as<Allocator>(tight);
    auto full                                    = tight_api.allocate(second).unwrap();
    static_cast<rstd::uint8_t*>(full.pointer)[0] = 42;
    rstd_assert(tight_api.shrink(full.pointer, second, first).is_err());
    rstd_assert(tight.statistics().live_bytes == usize(128));
    rstd_assert(static_cast<rstd::uint8_t*>(full.pointer)[0] == 42);
    tight_api.deallocate(full.pointer, second);

    auto empty      = BudgetAllocator<>::try_make(usize()).unwrap();
    auto zero       = Layout::array<u8>(usize()).unwrap();
    auto allocation = as<Allocator>(empty).allocate(zero).unwrap();
    rstd_assert(as<Allocator>(empty).allocate(first).is_err());
    as<Allocator>(empty).deallocate(allocation.pointer, zero);
    rstd_assert(empty.statistics().live_bytes == usize());
    rstd_assert(empty.statistics().allocations == usize(1));
    rstd_assert(empty.statistics().failures == usize(1));
}

void ownership() {
    Counts boxed_counts;
    {
        auto boxed = Box<Tracked>::try_make(boxed_counts, 42).unwrap();
        rstd_assert(boxed->value == 42);
        rstd_assert(reinterpret_cast<rstd::uintptr_t>(boxed.get()) % alignof(Tracked) == 0);
    }
    rstd_assert(boxed_counts.dropped == 1);
    {
        auto boxed = Box<Tracked>::make(boxed_counts, 43);
        rstd_assert(boxed->value == 43);
    }
    rstd_assert(boxed_counts.dropped == 2);
    Counts dyn_counts;
    {
        auto boxed    = Box<dyn<rstd::any::Any>>::try_make(Tracked(dyn_counts, 7)).unwrap();
        auto concrete = rstd::move(boxed).downcast<Tracked>().unwrap();
        rstd_assert(concrete->value == 7);
    }
    rstd_assert(dyn_counts.dropped == 1);
    {
        auto boxed    = Box<dyn<rstd::any::Any>>::make(Tracked(dyn_counts, 8));
        auto concrete = rstd::move(boxed).downcast<Tracked>().unwrap();
        rstd_assert(concrete->value == 8);
    }
    rstd_assert(dyn_counts.dropped == 2);
    Counts arc_counts;
    auto arc_dyn = rstd::sync::Arc<dyn<rstd::any::Any>>::try_make(Tracked(arc_counts, 9)).unwrap();
    auto weak    = arc_dyn.downgrade();
    auto other   = arc_dyn.clone();
    arc_dyn.reset();
    rstd_assert(arc_counts.dropped == 0 && ! weak.expired());
    auto value = rstd::any::downcast_ref<Tracked>(other.deref()).unwrap();
    rstd_assert(value->value == 9);
    rstd_assert(reinterpret_cast<rstd::uintptr_t>(value.as_raw_ptr()) % alignof(Tracked) == 0);
    other.reset();
    rstd_assert(arc_counts.dropped == 1 && weak.expired());
    weak.reset();

    auto arc   = rstd::sync::Arc<int>::try_make(42).unwrap();
    auto clone = arc.clone();
    arc.reset();
    rstd_assert(*clone == 42);
}

template<typename C>
void capacity_storage(BudgetAllocator<> budget) {
    using Error = ::alloc::collections::TryReserveError;
    auto empty  = C::try_with_capacity_in(usize(), budget.clone()).unwrap();
    rstd_assert(empty.is_empty() && empty.capacity() == usize());
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    auto legacy = C::new_in(budget.clone());
#pragma clang diagnostic pop
    rstd_assert(legacy.is_empty() && legacy.capacity() == usize());
    auto before   = budget.statistics().allocations;
    auto overflow = C::try_with_capacity_in(usize::MAX, budget.clone());
    rstd_assert(overflow.is_err() && overflow.unwrap_err() == Error::CapacityOverflow);
    rstd_assert(budget.statistics().allocations == before);
    auto failed = C::try_with_capacity_in(usize(65536), budget.clone());
    rstd_assert(failed.is_err() && failed.unwrap_err() == Error::AllocError);
    rstd_assert(budget.statistics().live_bytes == usize());
    {
        auto values = C::try_with_capacity_in(usize(8), budget.clone()).unwrap();
        rstd_assert(values.is_empty() && values.capacity() >= usize(8));
        rstd_assert(budget.statistics().live_bytes != usize());
    }
    rstd_assert(budget.statistics().live_bytes == usize());
}

void capacity_construction() {
    using A     = BudgetAllocator<>;
    using S     = rstd::hash::RandomState;
    using E     = ::alloc::collections::DefaultHashEqual<u32>;
    auto budget = A::try_make(usize(4096)).unwrap();
    capacity_storage<Vec<u8, A>>(budget.clone());
    capacity_storage<rstd::collections::HashTable<u32, A>>(budget.clone());
    capacity_storage<rstd::collections::HashMap<u32, u32, S, E, A>>(budget.clone());
    capacity_storage<rstd::collections::HashSet<u32, S, E, A>>(budget.clone());
    auto attempts  = budget.statistics().allocations;
    auto oversized = rstd::collections::HashTable<u8, A>::try_with_capacity_in(
        usize(isize::MAX.to_primitive()) / usize(4), budget.clone());
    rstd_assert(oversized.is_err());
    rstd_assert(oversized.unwrap_err() == ::alloc::collections::TryReserveError::CapacityOverflow);
    rstd_assert(budget.statistics().allocations == attempts);
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        auto readonly = rstd::bytes::Bytes::new_in(budget.clone());
        auto writable = rstd::bytes::BytesMut::new_in(budget.clone());
#pragma clang diagnostic pop
        rstd_assert(readonly.is_empty() && writable.is_empty());
        rstd_assert(budget.statistics().live_bytes != usize());
    }
    rstd_assert(budget.statistics().live_bytes == usize());
}

void erased_allocator() {
    using rstd::alloc::SharedAllocator;
    SharedAllocator global;
    auto            clone = global.clone();
    auto            small = Layout::array<u8>(usize(16)).unwrap();
    auto            large = Layout::array<u8>(usize(32)).unwrap();
    auto            value = global.allocate_zeroed(small).unwrap();
    rstd_assert(static_cast<unsigned char*>(value.pointer)[0] == 0);
    static_cast<unsigned char*>(value.pointer)[0] = 42;
    auto grown = clone.grow_zeroed(value.pointer, small, large).unwrap();
    rstd_assert(static_cast<unsigned char*>(grown.pointer)[0] == 42);
    rstd_assert(static_cast<unsigned char*>(grown.pointer)[16] == 0);
    auto shrunk = global.shrink(grown.pointer, large, small).unwrap();
    clone.deallocate(shrunk.pointer, small);
    auto budget   = BudgetAllocator<>::try_make(usize(64)).unwrap();
    auto owner    = SharedAllocator::make(budget.clone());
    auto retained = owner.clone();
    owner         = SharedAllocator {};
    auto memory   = retained.allocate(small).unwrap();
    rstd_assert(budget.statistics().live_bytes == usize(16));
    auto freed = rstd::thread::builder::Builder::make()
                     .spawn([allocator = rstd::move(retained), memory, small] {
                         allocator.deallocate(memory.pointer, small);
                     })
                     .unwrap();
    rstd::move(freed).join().unwrap();
    rstd_assert(budget.statistics().live_bytes == usize());
}

using namespace rstd::literals;
static void pool_lifetime() {
    using namespace rstd::alloc;
    auto pool   = SharedPoolAllocator<VirtualMemoryAllocator>::try_make_in(
                      VirtualMemoryAllocator {},
                      PoolOptions { .retained_bytes = usize(262144) },
                      VirtualMemoryAllocator {})
                      .unwrap();
    auto erased = SharedAllocator::try_make_in(VirtualMemoryAllocator {}, pool.clone()).unwrap();
    auto first = rstd::bytes::BytesMut::try_with_capacity_in(usize(16384), erased.clone()).unwrap();
    first.extend_from_slice("hello"_str.as_bytes());
    auto frozen = first.freeze();
    auto shared = frozen.clone();
    rstd_assert(rstd::move(frozen).try_into_mut().is_err());
    rstd_assert(pool.trim().slabs == usize());
    auto recovered = rstd::move(shared).try_into_mut().unwrap();
    rstd_assert(recovered.as_slice() == "hello"_str.as_bytes());
    recovered = {};
    rstd_assert(pool.statistics().live_bytes == usize());
    pool.trim();
    rstd_assert(pool.upstream_statistics().mapped_bytes == usize());
    auto zero  = Layout::from_size_align(usize(), usize(16)).unwrap();
    auto empty = pool.allocate(zero).unwrap();
    pool.deallocate(empty.pointer, zero);
    rstd_assert(pool.statistics().live_bytes == usize());
    const unsigned sizes[] { 1, 15, 16, 17, 31, 32, 33, 4095, 4096, 4097, 65535, 65536, 65537 };
    for (auto size : sizes) {
        auto boundary = Layout::from_size_align(usize(size), usize(16)).unwrap();
        auto block    = pool.allocate(boundary).unwrap();
        rstd_assert(reinterpret_cast<rstd::uintptr_t>(block.pointer) % 16 == 0);
        static_cast<unsigned char*>(block.pointer)[size - 1] = 42;
        pool.deallocate(block.pointer, boundary);
    }
    auto layout = Layout::from_size_align(usize(33), usize(16)).unwrap();
    auto api    = rstd::as<Allocator>(pool);
    auto block  = api.allocate(layout).unwrap();
    rstd::mem::memset(block.pointer, u8(0xff), layout.size);
    auto address = block.pointer;
    api.deallocate(block.pointer, layout);
    block = api.allocate_zeroed(layout).unwrap();
    rstd_assert(block.pointer == address);
    for (unsigned i = 0; i < 33; ++i)
        rstd_assert(static_cast<unsigned char*>(block.pointer)[i] == 0);
    api.deallocate(block.pointer, layout);
    pool.trim();
    auto small        = Layout::from_size_align(usize(64), usize(16)).unwrap();
    auto grown_layout = Layout::from_size_align(usize(128), usize(16)).unwrap();
    auto original     = api.allocate(small).unwrap();
    rstd::mem::memset(original.pointer, u8(42), small.size);
    auto grown = api.grow_zeroed(original.pointer, small, grown_layout).unwrap();
    for (unsigned i = 0; i < 128; ++i)
        rstd_assert(static_cast<unsigned char*>(grown.pointer)[i] == (i < 64 ? 42 : 0));
    api.deallocate(grown.pointer, grown_layout);
    pool.trim();
    auto large      = Layout::from_size_align(usize(70000), usize(64)).unwrap();
    auto allocation = api.allocate(large).unwrap();
    rstd_assert(reinterpret_cast<rstd::uintptr_t>(allocation.pointer) % 64 == 0);
    api.deallocate(allocation.pointer, large);
    rstd_assert(pool.upstream_statistics().mapped_bytes == usize());
    rstd_assert(api.allocate(Layout::from_size_align_unchecked(usize::MAX, usize(16))).is_err());
    auto typed =
        ::alloc::vec::Vec<u64, SharedPoolAllocator<VirtualMemoryAllocator>>::make_in(pool.clone());
    typed.reserve_exact(usize(4));
    typed.push(u64(42));
    auto aligned = rstd::bytes::BytesMut::from_vec(rstd::move(typed));
    aligned.reserve(usize(128));
    rstd_assert(reinterpret_cast<rstd::uintptr_t>(aligned.data()) % alignof(u64) == 0);
    VirtualMemoryAllocator source;
    auto page   = Layout::from_size_align(source.page_size() + usize(1), usize(16)).unwrap();
    auto mapped = source.allocate_zeroed(page).unwrap();
    rstd_assert(source.statistics().mapped_bytes == source.page_size() * usize(2));
    VirtualMemoryAllocator moved(rstd::move(source));
    rstd_assert(source.statistics().mapped_bytes == usize());
    rstd_assert(moved.statistics().mappings == usize(1));
    moved.deallocate(mapped.pointer, page);
    rstd_assert(moved.statistics().mapped_bytes == usize());
    rstd_assert(moved.statistics().released_bytes == moved.page_size() * usize(2));
}

static void pool_failures() {
    using namespace rstd::alloc;
    auto control = BudgetAllocator<>::try_make(usize(1)).unwrap();
    rstd_assert(SharedPoolAllocator<VirtualMemoryAllocator>::try_make_in(control.clone()).is_err());
    rstd_assert(control.statistics().live_bytes == usize());
    auto tight = BudgetAllocator<>::try_make(usize(4096)).unwrap();
    auto bytes = rstd::bytes::BytesMut::try_with_capacity_in(usize(65536), tight.clone());
    rstd_assert(bytes.is_err() && tight.statistics().live_bytes == usize());
    rstd_assert(tight.statistics().allocations == usize(1));
    auto none = BudgetAllocator<>::try_make(usize(1)).unwrap();
    rstd_assert(rstd::bytes::BytesMut::try_with_capacity_in(usize(64), none.clone()).is_err());
    rstd_assert(none.statistics().live_bytes == usize());
    auto owner_budget = BudgetAllocator<>::try_make(usize(1048576)).unwrap();
    {
        auto weak = [&] {
            auto pool =
                SharedPoolAllocator<BudgetAllocator<>>::try_make({}, owner_budget.clone()).unwrap();
            auto value = ::alloc::sync::Arc<usize>::try_make_in(pool.clone(), usize(42)).unwrap();
            return value.downgrade();
        }();
        rstd_assert(weak.expired());
        rstd_assert(owner_budget.statistics().live_bytes != usize());
    }
    rstd_assert(owner_budget.statistics().live_bytes == usize());
    auto upstream = BudgetAllocator<>::try_make(usize(262144)).unwrap();
    auto pool     = SharedPoolAllocator<BudgetAllocator<>>::try_make({}, upstream.clone()).unwrap();
    auto small    = Layout::from_size_align(usize(64), usize(16)).unwrap();
    auto large    = Layout::from_size_align(usize(524288), usize(16)).unwrap();
    auto api      = rstd::as<Allocator>(pool);
    auto allocation                                    = api.allocate(small).unwrap();
    static_cast<unsigned char*>(allocation.pointer)[0] = 42;
    rstd_assert(api.grow(allocation.pointer, small, large).is_err());
    rstd_assert(pool.statistics().grows == usize(1));
    rstd_assert(static_cast<unsigned char*>(allocation.pointer)[0] == 42);
    api.deallocate(allocation.pointer, small);
    pool.trim();
    rstd_assert(upstream.statistics().live_bytes == usize());
}
static void pool_threads() {
    using namespace rstd::alloc;
    auto pool    = SharedPoolAllocator<VirtualMemoryAllocator>::try_make_in(
                       VirtualMemoryAllocator {},
                       PoolOptions { .retained_bytes = usize() },
                       VirtualMemoryAllocator {})
                       .unwrap();
    auto layout  = Layout::from_size_align(usize(128), usize(16)).unwrap();
    auto pointer = pool.allocate(layout).unwrap().pointer;
    auto worker  = rstd::thread::builder::Builder::make()
                       .spawn([owner = pool.clone(), pointer, layout] {
                          owner.deallocate(pointer, layout);
                          for (int i = 0; i < 50; ++i) {
                              auto value = owner.allocate(layout).unwrap();
                              owner.deallocate(value.pointer, layout);
                          }
                       })
                       .unwrap();
    for (int i = 0; i < 50; ++i) {
        auto value = pool.allocate(layout).unwrap();
        pool.deallocate(value.pointer, layout);
    }
    rstd::move(worker).join().unwrap();
    rstd_assert(pool.statistics().live_bytes == usize());
    rstd_assert(pool.upstream_statistics().mapped_bytes == usize());
}

TEST(BudgetAllocator, PoolLifetime) {
    pool_lifetime();
}
TEST(BudgetAllocator, PoolFailures) {
    pool_failures();
}
TEST(BudgetAllocator, PoolThreads) {
    pool_threads();
}
TEST(BudgetAllocator, ErasedAllocator) {
    erased_allocator();
}
TEST(BudgetAllocator, ResizeAndClone) {
    resize_and_clone();
}
TEST(BudgetAllocator, Ownership) {
    ownership();
}
TEST(BudgetAllocator, CapacityConstruction) {
    capacity_construction();
}
TEST(BudgetAllocator, SharedThreadBudget) {
    auto shared  = BudgetAllocator<>::try_make(usize(1024)).unwrap();
    auto workers = Vec<rstd::thread::JoinHandle<void>>::make();
    for (int thread = 0; thread < 8; ++thread) {
        workers.push(rstd::thread::builder::Builder::make()
                         .spawn([owner = shared.clone()] {
                             auto layout = Layout::array<u8>(usize(64)).unwrap();
                             for (int i = 0; i < 1000; ++i) {
                                 auto memory = rstd::as<Allocator>(owner).allocate(layout).unwrap();
                                 rstd::as<Allocator>(owner).deallocate(memory.pointer, layout);
                             }
                         })
                         .unwrap());
    }
    for (auto& worker : workers) rstd::move(worker).join().unwrap();
    rstd_assert(shared.statistics().live_bytes == usize());
    rstd_assert(shared.statistics().allocations == usize(8000));
    rstd_assert(shared.statistics().peak_bytes <= usize(512));
}

} // namespace
