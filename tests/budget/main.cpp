#include <rstd/macro.hpp>
#include "objects.hpp"

import rstd;

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

int main() {
    resize_and_clone();
    ownership();
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
