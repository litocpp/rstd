#include <rstd/macro.hpp>
#include "../budget/objects.hpp"

import rstd;

using namespace rstd::prelude;

static bool fail_next = false;

extern "C" void* __real___rstd_alloc(rstd::size_t size, rstd::size_t align);

extern "C" void* __wrap___rstd_alloc(rstd::size_t size, rstd::size_t align) {
    if (fail_next) {
        fail_next = false;
        return nullptr;
    }
    return __real___rstd_alloc(size, align);
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

int main() {
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
}
