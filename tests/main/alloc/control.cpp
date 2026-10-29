#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;
using rstd::alloc::Allocation;
using rstd::alloc::Allocator;
using rstd::alloc::AllocError;
using rstd::alloc::Layout;

struct ControlCounts {
    usize  attempts;
    usize  allocations;
    usize  deallocations;
    usize  live_bytes;
    usize  handles;
    usize  invalid_releases;
    Layout last_layout;
};

struct ControlData {
    struct Block {
        void*  pointer {};
        Layout layout;
    } blocks[64];
    ControlCounts counts {};
    bool          fail_next {};
};

struct ControlDomain {
    rstd::sync::Mutex<ControlData> data;

    auto snapshot() const -> ControlCounts { return data.lock().unwrap()->counts; }
    void fail_next() { data.lock().unwrap()->fail_next = true; }
};

struct alignas(128) ControlSource {
    ControlDomain* domain;

    explicit ControlSource(ControlDomain& domain): domain(&domain) {
        ++domain.data.lock().unwrap()->counts.handles;
    }
    ControlSource(const ControlSource&) = delete;
    ControlSource(ControlSource&& other) noexcept: domain(rstd::exchange(other.domain, nullptr)) {}
    ~ControlSource() {
        if (domain) --domain->data.lock().unwrap()->counts.handles;
    }
};

namespace rstd
{
template<>
struct Impl<alloc::ThreadSafeAllocator, ControlSource> {};
template<>
struct Impl<Allocator, ControlSource> : DefaultInImpl<Allocator, ControlSource> {
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        auto state = this->self().domain->data.lock().unwrap();
        ++state->counts.attempts;
        if (rstd::exchange(state->fail_next, false)) return Err(AllocError {});
        auto result = as<Allocator>(::alloc::GLOBAL).allocate(layout);
        if (result.is_err()) return result;
        for (auto& block : state->blocks) {
            if (! block.pointer) {
                block = { result->pointer, layout };
                ++state->counts.allocations;
                state->counts.live_bytes += layout.size;
                state->counts.last_layout = layout;
                return result;
            }
        }
        as<Allocator>(::alloc::GLOBAL).deallocate(result->pointer, layout);
        rstd::panic { "test block capacity exceeded" };
    }
    void deallocate(void* pointer, Layout layout) const noexcept {
        auto state = this->self().domain->data.lock().unwrap();
        for (auto& block : state->blocks) {
            if (block.pointer == pointer) {
                if (layout.size != block.layout.size || layout.align != block.layout.align)
                    ++state->counts.invalid_releases;
                ++state->counts.deallocations;
                state->counts.live_bytes -= block.layout.size;
                as<Allocator>(::alloc::GLOBAL).deallocate(pointer, block.layout);
                block.pointer = nullptr;
                return;
            }
        }
        ++state->counts.invalid_releases;
    }
};
} // namespace rstd

using ControlledArena    = rstd::alloc::SharedBumpArena<ControlSource>;
using ControlledTracking = rstd::alloc::SharedTrackingAllocator<ControlSource>;
using ControlledBudget   = rstd::alloc::BudgetAllocator<ControlSource>;

template<typename Create>
void expect_control_failure(Create create) {
    ControlDomain control, data;
    control.fail_next();
    EXPECT_TRUE(create(ControlSource(control), ControlSource(data)).is_err());
    EXPECT_EQ(control.snapshot().attempts, usize(1));
    EXPECT_EQ(control.snapshot().allocations, usize());
    EXPECT_EQ(control.snapshot().handles, usize());
    EXPECT_EQ(data.snapshot().attempts, usize());
    EXPECT_EQ(data.snapshot().handles, usize());
}

template<typename Owner>
void expect_control_layout(const ControlDomain& control) {
    auto expected = Owner::template control_layout<ControlSource>().unwrap();
    auto actual   = control.snapshot().last_layout;
    EXPECT_EQ(actual.size, expected.size);
    EXPECT_EQ(actual.align, expected.align);
    EXPECT_GE(actual.align, usize(alignof(ControlSource)));
    EXPECT_EQ(control.snapshot().allocations, usize(1));
}

void expect_control_released(const ControlDomain& control) {
    auto counts = control.snapshot();
    EXPECT_EQ(counts.live_bytes, usize());
    EXPECT_EQ(counts.handles, usize());
    EXPECT_EQ(counts.allocations, counts.deallocations);
    EXPECT_EQ(counts.invalid_releases, usize());
}

TEST(AllocatorControl, CreationFailureDoesNotAllocateDataOrRetainSources) {
    expect_control_failure([](ControlSource control, ControlSource data) {
        return ControlledArena::try_make_in(rstd::move(control), usize(1024), rstd::move(data));
    });
    expect_control_failure([](ControlSource control, ControlSource data) {
        return ControlledTracking::try_make_in(rstd::move(control), rstd::move(data));
    });
    expect_control_failure([](ControlSource control, ControlSource data) {
        return ControlledBudget::try_make_in(rstd::move(control), usize(64), rstd::move(data));
    });
}

TEST(AllocatorControl, ArenaPayloadDiesBeforeLastWeakControlRelease) {
    using Handle = rstd::alloc::SharedArenaAllocator<ControlSource>;
    using Weak   = rstd::alloc::WeakSharedArena<ControlSource>;
    ControlDomain  control, data;
    Option<Handle> last;
    Option<Weak>   weak;
    {
        auto root =
            ControlledArena::try_make_in(ControlSource(control), usize(1024), ControlSource(data))
                .unwrap();
        expect_control_layout<ControlledArena>(control);
        EXPECT_EQ(data.snapshot().attempts, usize());
        auto handle = root.allocator();
        auto layout = Layout::array<u8>(usize(64)).unwrap();
        data.fail_next();
        EXPECT_TRUE(as<Allocator>(handle).allocate(layout).is_err());
        EXPECT_TRUE(as<Allocator>(handle).allocate(layout).is_ok());
        last = Some(handle.clone());
        weak = Some(root.downgrade());
    }
    EXPECT_FALSE(weak->expired());
    EXPECT_GT(data.snapshot().live_bytes, usize());
    auto worker = rstd::thread::spawn([last = rstd::move(last)]() mutable {
                      last = None();
                  }).unwrap();
    rstd::move(worker).join().unwrap();
    EXPECT_TRUE(weak->expired());
    expect_control_released(data);
    EXPECT_GT(control.snapshot().live_bytes, usize());
    EXPECT_EQ(control.snapshot().handles, usize(1));
    auto cleanup = rstd::thread::spawn([weak = rstd::move(weak)]() mutable {
                       weak = None();
                   }).unwrap();
    rstd::move(cleanup).join().unwrap();
    expect_control_released(control);
}

TEST(AllocatorControl, TrackingObserverRetainsOnlyControlAllocation) {
    ControlDomain                        control, data;
    Option<ControlledTracking>           last;
    Option<ControlledTracking::Observer> observer;
    auto                                 layout = Layout::array<u8>(usize(32)).unwrap();
    void*                                pointer;
    {
        auto root =
            ControlledTracking::try_make_in(ControlSource(control), ControlSource(data)).unwrap();
        expect_control_layout<ControlledTracking>(control);
        EXPECT_EQ(data.snapshot().attempts, usize());
        data.fail_next();
        EXPECT_TRUE(root.allocate(layout).is_err());
        EXPECT_EQ(root.snapshot().live_bytes, u64());
        pointer = root.allocate(layout).unwrap().pointer;
        EXPECT_EQ(root.snapshot().live_bytes, u64(32));
        last     = Some(root.clone());
        observer = Some(root.downgrade());
    }
    EXPECT_FALSE(observer->expired());
    auto worker = rstd::thread::spawn([last = rstd::move(last), pointer, layout]() mutable {
                      last->deallocate(pointer, layout);
                      last = None();
                  }).unwrap();
    rstd::move(worker).join().unwrap();
    EXPECT_TRUE(observer->expired());
    EXPECT_TRUE(observer->snapshot().is_none());
    expect_control_released(data);
    EXPECT_GT(control.snapshot().live_bytes, usize());
    auto cleanup = rstd::thread::spawn([observer = rstd::move(observer)]() mutable {
                       observer = None();
                   }).unwrap();
    rstd::move(cleanup).join().unwrap();
    expect_control_released(control);
}

TEST(AllocatorControl, BudgetExcludesControlAndPreservesResizeOverlapAccounting) {
    ControlDomain            control, data;
    Option<ControlledBudget> last;
    auto                     full = Layout::array<u8>(usize(32)).unwrap();
    auto                     half = Layout::array<u8>(usize(16)).unwrap();
    void*                    pointer;
    {
        auto root =
            ControlledBudget::try_make_in(ControlSource(control), usize(32), ControlSource(data))
                .unwrap();
        expect_control_layout<ControlledBudget>(control);
        EXPECT_EQ(root.statistics().live_bytes, usize());
        data.fail_next();
        EXPECT_TRUE(root.allocate(full).is_err());
        EXPECT_EQ(root.statistics().live_bytes, usize());
        pointer                                 = root.allocate(full).unwrap().pointer;
        static_cast<unsigned char*>(pointer)[0] = 71;
        EXPECT_TRUE(as<Allocator>(root).shrink(pointer, full, half).is_err());
        EXPECT_EQ(static_cast<unsigned char*>(pointer)[0], 71);
        EXPECT_EQ(root.statistics().live_bytes, usize(32));
        EXPECT_EQ(root.statistics().failures, usize(2));
        EXPECT_EQ(data.snapshot().attempts, usize(2));
        last = Some(root.clone());
    }
    auto worker = rstd::thread::spawn([last = rstd::move(last), pointer, full]() mutable {
                      last->deallocate(pointer, full);
                      last = None();
                  }).unwrap();
    rstd::move(worker).join().unwrap();
    expect_control_released(data);
    expect_control_released(control);
}
