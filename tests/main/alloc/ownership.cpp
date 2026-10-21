#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;
using rstd::alloc::Allocator;
using rstd::alloc::Allocation;
using rstd::alloc::AllocError;
using rstd::alloc::Layout;
using rstd::boxed::Box;
using alloc::sync::Arc;

struct OwnershipState {
    struct Block {
        void*  pointer {};
        Layout requested;
        Layout actual;
    } blocks[64];
    int   handles {};
    int   releases {};
    int   allocations {};
    int   allocation_attempts {};
    int   deallocations {};
    int   invalid_releases {};
    bool  fail {};
    usize extra;
    usize live;
};

struct OwnershipAllocator {
    OwnershipState* state;
    explicit OwnershipAllocator(OwnershipState& state): state(&state) { ++state.handles; }
    OwnershipAllocator(const OwnershipAllocator&) = delete;
    OwnershipAllocator(OwnershipAllocator&& other) noexcept
        : state(rstd::exchange(other.state, nullptr)) {}
    auto operator=(OwnershipAllocator&& other) noexcept -> OwnershipAllocator& {
        if (this != &other) {
            release();
            state = rstd::exchange(other.state, nullptr);
        }
        return *this;
    }
    ~OwnershipAllocator() { release(); }
    void release() {
        if (state && --state->handles == 0) ++state->releases;
    }
    auto clone() const -> OwnershipAllocator { return OwnershipAllocator(*state); }
};

namespace rstd
{
template<>
struct Impl<alloc::AllocatorClone, OwnershipAllocator> {};
template<>
struct Impl<alloc::TransferableAllocator, OwnershipAllocator> {};
template<>
struct Impl<clone::Clone, OwnershipAllocator> : DefaultInImpl<clone::Clone, OwnershipAllocator> {
    auto clone() const -> OwnershipAllocator { return this->self().clone(); }
};
template<>
struct Impl<alloc::Allocator, OwnershipAllocator>
    : DefaultInImpl<alloc::Allocator, OwnershipAllocator> {
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        auto& state = *this->self().state;
        ++state.allocation_attempts;
        if (state.fail) return Err(AllocError {});
        if (layout.size == usize()) return Ok(Allocation { layout.dangling(), usize() });
        auto actual = Layout::from_size_align(layout.size + state.extra, layout.align).unwrap();
        auto result = as<Allocator>(::alloc::GLOBAL).allocate(actual);
        if (result.is_err()) return result;
        auto allocation = result.unwrap_unchecked();
        rstd::mem::memset(allocation.pointer, u8(0xcd), allocation.size);
        for (auto& block : state.blocks) {
            if (! block.pointer) {
                block = { allocation.pointer, layout, actual };
                ++state.allocations;
                state.live += layout.size;
                return Ok(allocation);
            }
        }
        rstd::panic { "test block capacity exceeded" };
    }
    void deallocate(void* pointer, Layout layout) const noexcept {
        if (layout.size == usize()) return;
        auto& state = *this->self().state;
        for (auto& block : state.blocks) {
            if (block.pointer == pointer) {
                if (layout.align != block.requested.align || layout.size < block.requested.size ||
                    layout.size > block.actual.size)
                    ++state.invalid_releases;
                state.live -= block.requested.size;
                ++state.deallocations;
                as<Allocator>(::alloc::GLOBAL).deallocate(pointer, block.actual);
                block.pointer = nullptr;
                return;
            }
        }
        ++state.invalid_releases;
    }
};
} // namespace rstd

using SharedOwner = rstd::alloc::SharedTrackingAllocator<OwnershipAllocator>;
using OwnedBox    = Box<int, OwnershipAllocator>;

template<typename B>
concept HasPointerOnlyRaw = requires(B box) { rstd::move(box).into_raw(); };
template<typename A>
concept CanAllocateArc = requires(A allocator) { Arc<int>::make_in(rstd::move(allocator), 1); };
template<typename B>
concept HasClone = requires(const B& box) { box.clone(); };
static_assert(! HasPointerOnlyRaw<OwnedBox>);
static_assert(HasPointerOnlyRaw<Box<int>>);
static_assert(! CanAllocateArc<OwnershipAllocator>);
static_assert(! CanAllocateArc<rstd::alloc::VirtualMemoryAllocator>);
static_assert(CanAllocateArc<SharedOwner>);
static_assert(! HasClone<Box<int, rstd::alloc::VirtualMemoryAllocator>>);
static_assert(sizeof(Box<int>) == sizeof(int*));
static_assert(sizeof(OwnedBox) == 2 * sizeof(void*));
static_assert(sizeof(Arc<int>) == sizeof(void*));
static_assert(sizeof(alloc::sync::Weak<int>) == sizeof(void*));

struct alignas(256) OwnershipValue {
    int* drops;
    int  value;
    OwnershipValue(int& drops, int value): drops(&drops), value(value) {}
    OwnershipValue(const OwnershipValue&) = delete;
    OwnershipValue(OwnershipValue&& other) noexcept
        : drops(rstd::exchange(other.drops, nullptr)), value(other.value) {}
    ~OwnershipValue() {
        if (drops) ++*drops;
    }
};

TEST(AllocationOwnership, BoxCloneMoveAndRawKeepDomain) {
    OwnershipState first, second;
    {
        auto left  = OwnedBox::make_in(OwnershipAllocator(first), 11);
        auto right = OwnedBox::make_in(OwnershipAllocator(second), 22);
        left.clone_from(right);
        EXPECT_EQ(*left, 22);
        EXPECT_EQ(left.allocator().state, &first);
        auto global = left.clone_in(alloc::Global {});
        EXPECT_EQ(*global, 22);
        auto cloned = left.clone();
        EXPECT_EQ(cloned.allocator().state, &first);
        right = rstd::move(cloned);
        EXPECT_EQ(second.live, usize());
        EXPECT_EQ(right.allocator().state, &first);
        auto raw      = rstd::move(right).into_raw_with_allocator();
        auto restored = OwnedBox::from_raw_in(raw.get<0>(), rstd::move(raw.get<1>()));
        EXPECT_EQ(*restored, 22);
    }
    EXPECT_EQ(first.live, usize());
    EXPECT_EQ(second.live, usize());
    EXPECT_EQ(first.invalid_releases + second.invalid_releases, 0);
    EXPECT_EQ(first.handles + second.handles, 0);
}

TEST(AllocationOwnership, BoxDynDowncastAndFailurePreserveOwner) {
    OwnershipState state;
    int            drops = 0;
    {
        state.fail = true;
        OwnershipValue value(drops, 9);
        auto           failed = Box<rstd::dyn<rstd::any::Any>, OwnershipAllocator>::try_make_in(
            OwnershipAllocator(state), rstd::move(value));
        EXPECT_TRUE(failed.is_err());
        EXPECT_NE(value.drops, nullptr);
        EXPECT_EQ(drops, 0);
        state.fail = false;
        auto boxed = Box<rstd::dyn<rstd::any::Any>, OwnershipAllocator>::make_in(
            OwnershipAllocator(state), rstd::move(value));
        auto wrong = rstd::move(boxed).downcast<int>();
        ASSERT_TRUE(wrong.is_err());
        auto original = wrong.unwrap_err();
        auto concrete = rstd::move(original).downcast<OwnershipValue>().unwrap();
        EXPECT_EQ(concrete->value, 9);
        EXPECT_EQ(concrete.allocator().state, &state);
        EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(concrete.get()) % 256, 0u);
    }
    EXPECT_EQ(drops, 1);
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, DefaultResizeHandlesBothAlignmentDirectionsAndFailure) {
    OwnershipState state;
    auto           owner            = OwnershipAllocator(state);
    auto           original         = Layout::from_size_align(usize(32), usize(16)).unwrap();
    auto           larger_alignment = Layout::from_size_align(usize(32), usize(256)).unwrap();
    auto           allocation       = rstd::as<Allocator>(owner).allocate(original).unwrap();
    static_cast<unsigned char*>(allocation.pointer)[0] = 87;
    state.fail                                         = true;
    EXPECT_TRUE(
        rstd::as<Allocator>(owner).shrink(allocation.pointer, original, larger_alignment).is_err());
    EXPECT_EQ(static_cast<unsigned char*>(allocation.pointer)[0], 87);
    state.fail = false;
    auto aligned =
        rstd::as<Allocator>(owner).shrink(allocation.pointer, original, larger_alignment).unwrap();
    EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(aligned.pointer) % 256, 0u);
    auto smaller =
        rstd::as<Allocator>(owner).shrink(aligned.pointer, larger_alignment, original).unwrap();
    EXPECT_EQ(static_cast<unsigned char*>(smaller.pointer)[0], 87);
    auto grown =
        rstd::as<Allocator>(owner).grow(smaller.pointer, original, larger_alignment).unwrap();
    rstd::as<Allocator>(owner).deallocate(grown.pointer, larger_alignment);
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, VecBoxTransferRollsBackFailedShrinkAndKeepsAllocator) {
    OwnershipState state;
    state.extra = usize(17);
    {
        auto values = alloc::vec::Vec<int, OwnershipAllocator>::with_capacity_in(
            usize(20), OwnershipAllocator(state));
        values.push(3);
        values.push(5);
        auto pointer = values.data();
        state.fail   = true;
        auto failed  = values.try_into_boxed_slice();
        ASSERT_TRUE(failed.is_err());
        auto restored = failed.unwrap_err();
        EXPECT_EQ(restored.data(), pointer);
        EXPECT_EQ(restored.len(), usize(2));
        EXPECT_EQ(restored.capacity(), usize(20));
        EXPECT_EQ(restored[usize(1)], 5);
        state.fail = false;
        auto boxed = restored.try_into_boxed_slice().unwrap();
        EXPECT_EQ(boxed.allocator().state, &state);
        auto clone    = boxed.clone();
        auto iterator = rstd::as<rstd::iter::IntoIterator>(clone).into_iter();
        EXPECT_EQ(*iterator.next(), 3);
        auto vector = alloc::vec::Vec<int, OwnershipAllocator>::from_boxed_slice(rstd::move(boxed));
        EXPECT_EQ(vector.capacity(), usize(2));
        auto allocations = state.allocations;
        auto same        = vector.into_boxed_slice();
        EXPECT_EQ(state.allocations, allocations);
        EXPECT_EQ(same.as_ptr().len(), usize(2));
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, EmptyAndNontrivialBoxedSlicesReleaseMatchingLayouts) {
    OwnershipState state;
    int            drops = 0;
    {
        auto values = alloc::vec::Vec<OwnershipValue, OwnershipAllocator>::with_capacity_in(
            usize(4), OwnershipAllocator(state));
        values.emplace_back(drops, 1);
        values.emplace_back(drops, 2);
        auto boxed    = values.into_boxed_slice();
        auto iterator = rstd::as<rstd::iter::IntoIterator>(boxed).into_iter();
        auto first    = iterator.next();
        EXPECT_EQ(first->value, 1);
    }
    EXPECT_EQ(drops, 2);
    {
        auto empty = alloc::vec::Vec<int, OwnershipAllocator>::with_capacity_in(
            usize(10), OwnershipAllocator(state));
        auto boxed = empty.into_boxed_slice();
        EXPECT_EQ(state.live, usize());
        auto vector = alloc::vec::Vec<int, OwnershipAllocator>::from_boxed_slice(rstd::move(boxed));
        vector.push(7);
        EXPECT_EQ(vector.allocator().state, &state);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, RcFallibleDynAndLastWeakKeepBackend) {
    OwnershipState                             state;
    int                                        drops = 0;
    alloc::rc::Weak<rstd::dyn<rstd::any::Any>> weak;
    {
        OwnershipValue input(drops, 31);
        state.fail  = true;
        auto failed = alloc::rc::try_make_rc_in<rstd::dyn<rstd::any::Any>>(
            OwnershipAllocator(state), rstd::move(input));
        EXPECT_TRUE(failed.is_err());
        EXPECT_NE(input.drops, nullptr);
        state.fail = false;
        auto owner = alloc::rc::make_rc_in<rstd::dyn<rstd::any::Any>>(OwnershipAllocator(state),
                                                                      rstd::move(input));
        auto allocations = state.allocations;
        weak             = owner.downgrade();
        auto copy        = owner.clone();
        auto raw         = copy.into_raw();
        auto restored    = decltype(owner)::from_raw(rstd::move(raw));
        EXPECT_EQ(state.allocations, allocations);
    }
    EXPECT_EQ(drops, 1);
    EXPECT_TRUE(weak.expired());
    EXPECT_TRUE(weak.upgrade().is_none());
    EXPECT_GT(state.live, usize());
    EXPECT_EQ(state.handles, 1);
    weak = {};
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.handles, 0);
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, ArcRawAndLastWeakReleaseOnAnotherThread) {
    OwnershipState state;
    int            drops = 0;
    auto           root  = Option<SharedOwner>(Some(SharedOwner::make(OwnershipAllocator(state))));
    auto           observer = root->downgrade();
    auto           owner    = Arc<OwnershipValue>::make_in(root->clone(), drops, 45);
    auto           weak     = owner.downgrade();
    auto           before   = root->snapshot();
    auto           raw      = owner.into_raw().into_raw();
    auto           restored =
        Arc<OwnershipValue>::from_raw(alloc::sync::ArcRaw<OwnershipValue>::from_raw(raw));
    EXPECT_EQ(restored->value, 45);
    EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(restored.as_ptr().as_raw_ptr()) % 256, 0u);
    auto clone = restored.clone();
    EXPECT_EQ(root->snapshot().allocations, before.allocations);
    clone.reset();
    root = None();
    restored.reset();
    EXPECT_EQ(drops, 1);
    EXPECT_TRUE(weak.expired());
    EXPECT_FALSE(observer.expired());
    auto worker = rstd::thread::spawn([weak = rstd::move(weak)]() mutable {
                      weak.reset();
                  }).unwrap();
    EXPECT_TRUE(rstd::move(worker).join().is_ok());
    EXPECT_TRUE(observer.expired());
    EXPECT_TRUE(observer.snapshot().is_none());
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.handles, 0);
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, ArcDynFailureUninitAndTryUnwrap) {
    OwnershipState state;
    auto           root  = SharedOwner::make(OwnershipAllocator(state));
    int            drops = 0;
    {
        OwnershipValue input(drops, 7);
        state.fail  = true;
        auto failed = Arc<rstd::dyn<rstd::any::Any>>::try_make_in(root.clone(), rstd::move(input));
        EXPECT_TRUE(failed.is_err());
        EXPECT_NE(input.drops, nullptr);
        state.fail = false;
        auto dyn   = Arc<rstd::dyn<rstd::any::Any>>::make_in(root.clone(), rstd::move(input));
        auto weak  = dyn.downgrade();
        dyn.reset();
        EXPECT_EQ(drops, 1);
        EXPECT_FALSE(bool(weak.upgrade()));
        weak.reset();
        auto uninit = Arc<int>::make_uninit_in(root.clone());
        uninit->write(61);
        auto initialized = uninit.assume_init();
        auto last        = initialized.downgrade();
        EXPECT_EQ(initialized.try_unwrap().unwrap(), 61);
        EXPECT_TRUE(last.expired());
        last.reset();
    }
    EXPECT_EQ(root.snapshot().live_bytes, u64());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, SharedCloneWorksForVecMapSetAndErasedAllocator) {
    OwnershipState first, second;
    auto           left  = SharedOwner::make(OwnershipAllocator(first));
    auto           right = SharedOwner::make(OwnershipAllocator(second));
    {
        auto vector = alloc::vec::Vec<int, SharedOwner>::new_in(left.clone());
        vector.push(1);
        auto copy  = vector.clone();
        auto cross = vector.clone_in(alloc::Global {});
        EXPECT_EQ(cross[usize()], 1);
        using Map = alloc::collections::HashMap<int,
                                                int,
                                                rstd::hash::RandomState,
                                                alloc::collections::DefaultHashEqual<int>,
                                                SharedOwner>;
        auto map  = Map::new_in(left.clone());
        map.insert(3, 9);
        auto copied = map.clone();
        auto target = Map::new_in(right.clone());
        target.clone_from(map);
        EXPECT_GT(right.snapshot().live_bytes, u64());
        EXPECT_EQ(*target.get(3).unwrap(), 9);
        using Set = alloc::collections::HashSet<int,
                                                rstd::hash::RandomState,
                                                alloc::collections::DefaultHashEqual<int>,
                                                SharedOwner>;
        auto set  = Set::new_in(left.clone());
        set.insert(4);
        auto cross_set = set.clone_in(alloc::Global {});
        EXPECT_TRUE(cross_set.contains(4));
        auto cloned = set.clone();
        auto iter   = rstd::as<rstd::iter::IntoIterator>(cloned).into_iter();
        EXPECT_EQ(*iter.next(), 4);
        auto erased = alloc::sync::SharedAllocator::make(left.clone());
        auto box    = Box<int, decltype(erased)>::make_in(erased.clone(), 17);
        auto arc    = Arc<int>::make_in(erased.clone(), 19);
        EXPECT_EQ(*box, 17);
        EXPECT_EQ(*arc, 19);
    }
    EXPECT_EQ(left.snapshot().live_bytes, u64());
    EXPECT_EQ(right.snapshot().live_bytes, u64());
    EXPECT_EQ(first.invalid_releases + second.invalid_releases, 0);
}

TEST(AllocationOwnership, SharedVirtualMemoryKeepsLogicalAndMappedBytesSeparate) {
    auto allocator =
        rstd::alloc::SharedTrackingAllocator<rstd::alloc::VirtualMemoryAllocator>::make();
    {
        auto value = Box<int, decltype(allocator)>::make_in(allocator.clone(), 1);
        EXPECT_EQ(allocator.snapshot().live_bytes, u64(sizeof(int)));
        EXPECT_GT(allocator.upstream_statistics().mapped_bytes, usize(sizeof(int)));
    }
    EXPECT_EQ(allocator.snapshot().live_bytes, u64());
    EXPECT_EQ(allocator.upstream_statistics().mapped_bytes, usize());
}

struct ForwardCounts {
    rstd::sync::atomic::Atomic<usize> calls[6];
};
struct ForwardAllocator {
    ForwardCounts* counts;
    explicit ForwardAllocator(ForwardCounts& counts): counts(&counts) {}
    ForwardAllocator(const ForwardAllocator&)     = delete;
    ForwardAllocator(ForwardAllocator&&) noexcept = default;
    void called(int index) const {
        counts->calls[index].fetch_add(usize(1), rstd::sync::atomic::Ordering::Relaxed);
    }
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        called(0);
        return rstd::as<Allocator>(alloc::GLOBAL).allocate(layout);
    }
    auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
        called(1);
        return rstd::as<Allocator>(alloc::GLOBAL).allocate_zeroed(layout);
    }
    void deallocate(void* pointer, Layout layout) const noexcept {
        called(2);
        rstd::as<Allocator>(alloc::GLOBAL).deallocate(pointer, layout);
    }
    auto grow(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        called(3);
        return rstd::as<Allocator>(alloc::GLOBAL).grow(pointer, old_layout, new_layout);
    }
    auto grow_zeroed(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        called(4);
        return rstd::as<Allocator>(alloc::GLOBAL).grow_zeroed(pointer, old_layout, new_layout);
    }
    auto shrink(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        called(5);
        return rstd::as<Allocator>(alloc::GLOBAL).shrink(pointer, old_layout, new_layout);
    }
};

template<>
struct rstd::Impl<rstd::alloc::ThreadSafeAllocator, ForwardAllocator> {};

struct alignas(512) AlignedAllocator {
    SharedOwner owner;
};
template<>
struct rstd::Impl<rstd::alloc::ThreadSafeAllocator, AlignedAllocator> {};
template<>
struct rstd::Impl<Allocator, AlignedAllocator> : rstd::DefaultInImpl<Allocator, AlignedAllocator> {
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        return this->self().owner.allocate(layout);
    }
    void deallocate(void* pointer, Layout layout) const noexcept {
        this->self().owner.deallocate(pointer, layout);
    }
};

static_assert(! CanAllocateArc<Arc<rstd::dyn<Allocator>>>);
static_assert(CanAllocateArc<rstd::alloc::SharedAllocator>);
static_assert(! rstd::Impled<rstd::alloc::TrackingAllocator<>, rstd::alloc::ThreadSafeAllocator>);
static_assert(rstd::Impled<rstd::alloc::BudgetAllocator<>, rstd::alloc::ThreadSafeAllocator>);
static_assert(rstd::Impled<rstd::alloc::SharedArenaAllocator<rstd::alloc::VirtualMemoryAllocator>,
                           rstd::alloc::ThreadSafeAllocator>);

template<typename A>
void check_forwarding(A allocator, ForwardCounts& counts) {
    auto small  = Layout::array<u8>(usize(16)).unwrap();
    auto medium = Layout::array<u8>(usize(32)).unwrap();
    auto large  = Layout::array<u8>(usize(64)).unwrap();
    auto raw    = rstd::as<Allocator>(allocator).allocate(small).unwrap();
    raw         = rstd::as<Allocator>(allocator).grow(raw.pointer, small, medium).unwrap();
    raw         = rstd::as<Allocator>(allocator).grow_zeroed(raw.pointer, medium, large).unwrap();
    EXPECT_EQ(static_cast<unsigned char*>(raw.pointer)[63], 0);
    raw = rstd::as<Allocator>(allocator).shrink(raw.pointer, large, small).unwrap();
    rstd::as<Allocator>(allocator).deallocate(raw.pointer, small);
    raw = rstd::as<Allocator>(allocator).allocate_zeroed(small).unwrap();
    EXPECT_EQ(static_cast<unsigned char*>(raw.pointer)[0], 0);
    rstd::as<Allocator>(allocator).deallocate(raw.pointer, small);
    for (int index = 0; index < 6; ++index)
        EXPECT_EQ(counts.calls[index].load(rstd::sync::atomic::Ordering::Relaxed),
                  usize(index == 2 ? 2 : 1));
}

TEST(AllocationOwnership, ArcRcDynAndTrackingForwardEveryOperation) {
    ForwardCounts arc, rc, dyn, tracked, arc_dyn, rc_dyn;
    auto          arc_backend = Arc<ForwardAllocator>::make(arc);
    auto          rc_backend  = alloc::rc::make_rc<ForwardAllocator>(rc);
    check_forwarding(arc_backend.clone(), arc);
    check_forwarding(rc_backend.clone(), rc);
    auto dynamic_arc = Arc<rstd::dyn<Allocator>>::make(ForwardAllocator(arc_dyn));
    auto dynamic_rc  = alloc::rc::make_rc<rstd::dyn<Allocator>>(ForwardAllocator(rc_dyn));
    check_forwarding(dynamic_arc.clone(), arc_dyn);
    check_forwarding(dynamic_rc.clone(), rc_dyn);
    auto erased = rstd::alloc::SharedAllocator::make(ForwardAllocator(dyn));
    check_forwarding(erased.clone(), dyn);
    auto tracking =
        rstd::alloc::SharedTrackingAllocator<ForwardAllocator>::make(ForwardAllocator(tracked));
    check_forwarding(tracking.clone(), tracked);
    EXPECT_EQ(tracking.snapshot().grows, u64(2));
    EXPECT_EQ(tracking.snapshot().shrinks, u64(1));
    EXPECT_EQ(tracking.snapshot().live_bytes, u64());
}

TEST(AllocationOwnership, ArcAndRcSupportOveralignedAllocatorStorage) {
    OwnershipState state;
    auto           root = SharedOwner::make(OwnershipAllocator(state));
    {
        auto arc      = Arc<int>::make_in(AlignedAllocator { root.clone() }, 12);
        auto rc       = alloc::rc::make_rc_in<int>(AlignedAllocator { root.clone() }, 13);
        auto arc_weak = arc.downgrade();
        auto rc_weak  = rc.downgrade();
        EXPECT_EQ(*arc, 12);
        EXPECT_EQ(*rc, 13);
        arc.reset();
        rc.reset();
        EXPECT_GT(root.snapshot().live_bytes, u64());
    }
    EXPECT_EQ(root.snapshot().live_bytes, u64());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, SharedArenaRootCanDieBeforeBoxVecAndArc) {
    using Root = rstd::alloc::SharedBumpArena<rstd::alloc::VirtualMemoryAllocator>;
    using A    = rstd::alloc::SharedArenaAllocator<rstd::alloc::VirtualMemoryAllocator>;
    Option<Box<int, A>>             box;
    Option<alloc::vec::Vec<int, A>> vector;
    Option<Arc<int>>                arc;
    auto                            weak = alloc::sync::Weak<int>::make();
    Option<rstd::alloc::WeakSharedArena<rstd::alloc::VirtualMemoryAllocator>> observer;
    {
        auto root = Root::try_make(usize(4096)).unwrap();
        observer  = Some(root.downgrade());
        box       = Some(Box<int, A>::make_in(root.allocator(), 4));
        vector    = Some(alloc::vec::Vec<int, A>::new_in(root.allocator()));
        vector->push(5);
        auto copy      = vector->clone();
        auto iterator  = rstd::move(copy).into_iter();
        auto collected = rstd::move(iterator).collect<alloc::vec::Vec<int, A>>();
        EXPECT_EQ(collected[usize()], 5);
        arc  = Some(Arc<int>::make_in(root.allocator(), 6));
        weak = (*arc).downgrade();
        EXPECT_GT(root.statistics().upstream.mapped_bytes, usize());
    }
    EXPECT_EQ(**box, 4);
    EXPECT_EQ((*vector)[usize()], 5);
    box    = None();
    vector = None();
    arc    = None();
    EXPECT_FALSE(observer->expired());
    weak.reset();
    EXPECT_TRUE(observer->expired());
}

TEST(AllocationOwnership, BudgetGrowthKeepsOldAndNewOverlapSemantics) {
    auto budget  = rstd::alloc::BudgetAllocator<>::try_make(usize(96)).unwrap();
    auto tracked = rstd::alloc::SharedTrackingAllocator<decltype(budget)>::make(budget.clone());
    auto small   = Layout::array<u8>(usize(40)).unwrap();
    auto large   = Layout::array<u8>(usize(64)).unwrap();
    auto raw     = tracked.allocate(small).unwrap();
    static_cast<unsigned char*>(raw.pointer)[0] = 99;
    EXPECT_TRUE(tracked.grow(raw.pointer, small, large).is_err());
    EXPECT_EQ(static_cast<unsigned char*>(raw.pointer)[0], 99);
    EXPECT_EQ(budget.statistics().live_bytes, usize(40));
    EXPECT_EQ(tracked.snapshot().failures, u64(1));
    EXPECT_EQ(tracked.snapshot().live_bytes, u64(40));
    tracked.deallocate(raw.pointer, small);
    {
        auto box  = Box<int, decltype(budget)>::make_in(budget.clone(), 7);
        auto copy = box.clone();
        EXPECT_EQ(*copy, 7);
    }
    EXPECT_EQ(budget.statistics().live_bytes, usize());
}

TEST(AllocationOwnership, ConcurrentSnapshotsAndCrossThreadFreeRemainConsistent) {
    auto root     = rstd::alloc::SharedTrackingAllocator<>::make();
    auto done     = Arc<rstd::sync::atomic::Atomic<usize>>::make(usize());
    auto producer = rstd::thread::spawn([allocator = root.clone(), done = done.clone()]() mutable {
                        for (int i = 0; i < 100; ++i) {
                            auto box = Box<int, decltype(allocator)>::make_in(allocator.clone(), i);
                            rstd::hint::black_box(box.get());
                        }
                        done->store(usize(1), rstd::sync::atomic::Ordering::Release);
                    }).unwrap();
    while (done->load(rstd::sync::atomic::Ordering::Acquire) == usize()) {
        auto snapshot = root.snapshot();
        EXPECT_LE(snapshot.live_bytes, snapshot.peak_live_bytes);
        EXPECT_FALSE(snapshot.overflow);
    }
    EXPECT_TRUE(rstd::move(producer).join().is_ok());
    auto box      = Box<int, decltype(root)>::make_in(root.clone(), 9);
    auto consumer = rstd::thread::spawn([value = rstd::move(box)] {
                        EXPECT_EQ(*value, 9);
                    }).unwrap();
    EXPECT_TRUE(rstd::move(consumer).join().is_ok());
    EXPECT_EQ(root.snapshot().live_bytes, u64());
    EXPECT_EQ(root.snapshot().allocations, u64(101));
    EXPECT_EQ(root.snapshot().deallocations, u64(101));
}

TEST(AllocationOwnership, OuterVecDoesNotAccountForNestedStringHeap) {
    using namespace rstd::literals;
    auto root   = rstd::alloc::SharedTrackingAllocator<>::make();
    auto values = alloc::vec::Vec<String, decltype(root)>::with_capacity_in(usize(2), root.clone());
    values.push("a long text whose storage remains owned by the Global String allocator"_Str);
    EXPECT_EQ(root.snapshot().live_bytes, u64(sizeof(String) * 2));
    EXPECT_EQ(root.snapshot().allocations, u64(1));
    auto copy = values.clone_in(alloc::Global {});
    EXPECT_EQ(copy[usize()], values[usize()]);
}

TEST(AllocationOwnership, TypedBoxBecomesDynWithoutMovingPayload) {
    OwnershipState state;
    int            drops = 0;
    {
        auto sized =
            Box<OwnershipValue, OwnershipAllocator>::make_in(OwnershipAllocator(state), drops, 42);
        auto pointer     = sized.get();
        auto allocations = state.allocations;
        auto dynamic     = rstd::move(sized).into_dyn<rstd::dyn<rstd::any::Any>>();
        auto restored    = rstd::move(dynamic).downcast<OwnershipValue>().unwrap();
        EXPECT_EQ(restored.get(), pointer);
        EXPECT_EQ(state.allocations, allocations);
    }
    EXPECT_EQ(drops, 1);
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, MoveOnlyBoxAndByteArrayCloning) {
    auto vm = Box<int, rstd::alloc::VirtualMemoryAllocator>::make_in(
        rstd::alloc::VirtualMemoryAllocator {}, 8);
    auto copied = vm.clone_in(alloc::Global {});
    EXPECT_EQ(*copied, 8);
    EXPECT_EQ(vm.allocator().statistics().mappings, usize(1));
    auto values = Vec<u8> {};
    values.push(u8(3));
    values.push(u8(9));
    auto boxed = values.into_boxed_slice();
    auto clone = boxed.clone();
    EXPECT_EQ(u8(clone.as_ptr()[usize(1)]), u8(9));
}

TEST(AllocationOwnership, PartialOwningIteratorCollectRetainsSharedAllocator) {
    auto root = rstd::alloc::SharedTrackingAllocator<>::make();
    using V   = alloc::vec::Vec<int, decltype(root)>;
    {
        auto values = V::with_capacity_in(usize(64), root.clone());
        for (int i = 0; i < 16; ++i) values.push(int(i));
        auto iterator = rstd::move(values).into_iter();
        EXPECT_EQ(*iterator.next(), 0);
        auto collected = rstd::move(iterator).collect<V>();
        EXPECT_EQ(collected.len(), usize(15));
        EXPECT_EQ(collected[usize()], 1);
        EXPECT_GT(root.snapshot().allocations, u64(1));
    }
    EXPECT_EQ(root.snapshot().live_bytes, u64());
}

#if defined(__cpp_exceptions)
struct ThrowsOnConstruction {
    explicit ThrowsOnConstruction(int) { throw 23; }
};
struct ThrowCloneState {
    int live {};
    int clones {};
    int throw_at {};
};
struct ThrowCloneValue {
    ThrowCloneState* state;
    explicit ThrowCloneValue(ThrowCloneState& state): state(&state) { ++state.live; }
    ThrowCloneValue(const ThrowCloneValue&) = delete;
    ThrowCloneValue(ThrowCloneValue&& other) noexcept: state(other.state) { ++state->live; }
    auto operator=(ThrowCloneValue&& other) noexcept -> ThrowCloneValue& {
        --state->live;
        state = other.state;
        ++state->live;
        return *this;
    }
    ~ThrowCloneValue() { --state->live; }
    auto clone() const -> ThrowCloneValue {
        if (++state->clones == state->throw_at) throw 31;
        return ThrowCloneValue(*state);
    }
};
template<>
struct rstd::Impl<rstd::clone::Clone, ThrowCloneValue>
    : rstd::DefaultInImpl<rstd::clone::Clone, ThrowCloneValue> {
    auto clone() const -> ThrowCloneValue { return this->self().clone(); }
};
TEST(AllocationOwnership, ConstructionExceptionsReleaseBoxArcAndRcStorage) {
    OwnershipState state;
    auto           root   = SharedOwner::make(OwnershipAllocator(state));
    int            caught = 0;
    try {
        auto value = Box<ThrowsOnConstruction, OwnershipAllocator>::try_make_in(
            OwnershipAllocator(state), 1);
    } catch (int e) {
        EXPECT_EQ(e, 23);
        ++caught;
    }
    try {
        auto value = Arc<ThrowsOnConstruction>::try_make_in(root.clone(), 1);
    } catch (int e) {
        EXPECT_EQ(e, 23);
        ++caught;
    }
    try {
        auto value = alloc::rc::try_make_rc_in<ThrowsOnConstruction>(OwnershipAllocator(state), 1);
    } catch (int e) {
        EXPECT_EQ(e, 23);
        ++caught;
    }
    EXPECT_EQ(caught, 3);
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}
TEST(AllocationOwnership, ArrayCloneExceptionDropsOnlyInitializedCopies) {
    OwnershipState  state;
    ThrowCloneState values;
    {
        auto vec = alloc::vec::Vec<ThrowCloneValue, OwnershipAllocator>::with_capacity_in(
            usize(3), OwnershipAllocator(state));
        for (int i = 0; i < 3; ++i) vec.emplace_back(values);
        auto boxed      = vec.into_boxed_slice();
        auto before     = state.live;
        values.throw_at = 2;
        int caught      = 0;
        try {
            auto clone = boxed.clone();
        } catch (int e) {
            EXPECT_EQ(e, 31);
            ++caught;
        }
        EXPECT_EQ(caught, 1);
        EXPECT_EQ(values.live, 3);
        EXPECT_EQ(state.live, before);
    }
    EXPECT_EQ(values.live, 0);
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}
#endif

struct InPlaceAllocator {
    OwnershipAllocator owner;
};
template<>
struct rstd::Impl<Allocator, InPlaceAllocator> : rstd::DefaultInImpl<Allocator, InPlaceAllocator> {
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        return rstd::as<Allocator>(this->self().owner).allocate(layout);
    }
    void deallocate(void* pointer, Layout layout) const noexcept {
        rstd::as<Allocator>(this->self().owner).deallocate(pointer, layout);
    }
    auto shrink(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        auto& state = *this->self().owner.state;
        for (auto& block : state.blocks) {
            if (block.pointer == pointer && old_layout.align == new_layout.align) {
                state.live -= old_layout.size - new_layout.size;
                block.requested = new_layout;
                return Ok(Allocation { pointer, block.actual.size });
            }
        }
        return Err(AllocError {});
    }
};

struct ZeroStorageAllocator {
    int* live;
};
template<>
struct rstd::Impl<Allocator, ZeroStorageAllocator>
    : rstd::DefaultInImpl<Allocator, ZeroStorageAllocator> {
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        auto physical = layout;
        if (physical.size == usize()) physical.size = usize(1);
        auto result = rstd::as<Allocator>(alloc::GLOBAL).allocate(physical);
        if (result.is_err()) return result;
        ++*this->self().live;
        return Ok(Allocation { result.unwrap_unchecked().pointer, layout.size });
    }
    void deallocate(void* pointer, Layout layout) const noexcept {
        if (layout.size == usize()) layout.size = usize(1);
        --*this->self().live;
        rstd::as<Allocator>(alloc::GLOBAL).deallocate(pointer, layout);
    }
};

TEST(AllocationOwnership, InPlaceShrinkAndEmptyBoxNeedNoExtraAllocation) {
    OwnershipState state;
    {
        auto vector = alloc::vec::Vec<int, InPlaceAllocator>::with_capacity_in(
            usize(16), InPlaceAllocator { OwnershipAllocator(state) });
        vector.push(77);
        auto pointer = vector.data();
        auto boxed   = vector.into_boxed_slice();
        EXPECT_EQ(boxed.get(), pointer);
        EXPECT_EQ(state.allocations, 1);
    }
    EXPECT_EQ(state.invalid_releases, 0);
    EXPECT_EQ(state.live, usize());
    auto tracking = rstd::alloc::SharedTrackingAllocator<>::make();
    {
        auto empty  = alloc::vec::Vec<int, decltype(tracking)>::new_in(tracking.clone());
        auto boxed  = empty.into_boxed_slice();
        auto copied = boxed.clone();
        auto restored =
            alloc::vec::Vec<int, decltype(tracking)>::from_boxed_slice(rstd::move(copied));
        EXPECT_TRUE(restored.is_empty());
    }
    EXPECT_EQ(tracking.snapshot().allocations, u64());
    EXPECT_EQ(tracking.snapshot().deallocations, u64());
}

TEST(AllocationOwnership, DefaultResizeReleasesZeroSizedBackendAllocations) {
    int  live      = 0;
    auto allocator = ZeroStorageAllocator { &live };
    auto zero      = Layout::from_size_align(usize(), usize(8)).unwrap();
    auto small     = Layout::from_size_align(usize(8), usize(8)).unwrap();
    auto raw       = rstd::as<Allocator>(allocator).allocate(zero).unwrap();
    EXPECT_EQ(live, 1);
    raw = rstd::as<Allocator>(allocator).grow(raw.pointer, zero, small).unwrap();
    EXPECT_EQ(live, 1);
    raw = rstd::as<Allocator>(allocator).shrink(raw.pointer, small, zero).unwrap();
    EXPECT_EQ(live, 1);
    raw = rstd::as<Allocator>(allocator).grow_zeroed(raw.pointer, zero, small).unwrap();
    EXPECT_EQ(live, 1);
    EXPECT_EQ(static_cast<unsigned char*>(raw.pointer)[7], 0);
    rstd::as<Allocator>(allocator).deallocate(raw.pointer, small);
    EXPECT_EQ(live, 0);
}

TEST(AllocationOwnership, DynErrorRawRoundtripPreservesFormattingAndAllocator) {
    OwnershipState state;
    using B = Box<rstd::dyn<rstd::error::Error>, OwnershipAllocator>;
    {
        auto boxed    = B::make_in(OwnershipAllocator(state), rstd::alloc::AllocError {});
        auto parts    = rstd::move(boxed).into_raw_with_allocator();
        auto restored = B::from_raw_in(parts.get<0>(), rstd::move(parts.get<1>()));
        auto text     = rstd::format("{}", restored);
        using namespace rstd::literals;
        EXPECT_EQ(text.as_str(), "memory allocation failed"_str);
        auto concrete = rstd::move(restored).downcast<rstd::alloc::AllocError>().unwrap();
        EXPECT_EQ(concrete.allocator().state, &state);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, ZeroedAllocationInitializesReturnedExcessCapacity) {
    OwnershipState state;
    state.extra    = usize(17);
    auto allocator = OwnershipAllocator(state);
    auto small     = Layout::array<u8>(usize(8)).unwrap();
    auto large     = Layout::array<u8>(usize(16)).unwrap();
    auto raw       = rstd::as<Allocator>(allocator).allocate_zeroed(small).unwrap();
    for (rstd::size_t i = 0; i < raw.size.to_primitive(); ++i)
        EXPECT_EQ(static_cast<unsigned char*>(raw.pointer)[i], 0);
    static_cast<unsigned char*>(raw.pointer)[0] = 42;
    raw = rstd::as<Allocator>(allocator).grow_zeroed(raw.pointer, small, large).unwrap();
    EXPECT_EQ(static_cast<unsigned char*>(raw.pointer)[0], 42);
    for (rstd::size_t i = small.size.to_primitive(); i < raw.size.to_primitive(); ++i)
        EXPECT_EQ(static_cast<unsigned char*>(raw.pointer)[i], 0);
    rstd::as<Allocator>(allocator).deallocate(raw.pointer, large);
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

#if defined(RSTD_TEST_ALLOCATOR_FAILURE)
// Enabled only by the explicit --wrap=__rstd_alloc test link configuration.
static rstd::sync::atomic::Atomic<bool> fail_next_runtime_allocation { false };
extern "C" void*                        __real___rstd_alloc(rstd::size_t size, rstd::size_t align);
extern "C" void*                        __wrap___rstd_alloc(rstd::size_t size, rstd::size_t align) {
    if (fail_next_runtime_allocation.exchange(false, rstd::sync::atomic::Ordering::Relaxed))
        return nullptr;
    return __real___rstd_alloc(size, align);
}

TEST(AllocationOwnership, BootstrapFailureInjectionReleasesConsumedAllocator) {
    OwnershipState state;
    fail_next_runtime_allocation.store(true, rstd::sync::atomic::Ordering::Relaxed);
    auto tracking = SharedOwner::try_make(OwnershipAllocator(state));
    EXPECT_TRUE(tracking.is_err());
    EXPECT_FALSE(fail_next_runtime_allocation.load(rstd::sync::atomic::Ordering::Relaxed));
    EXPECT_EQ(state.handles, 0);
    fail_next_runtime_allocation.store(true, rstd::sync::atomic::Ordering::Relaxed);
    auto arena = rstd::alloc::SharedBumpArena<OwnershipAllocator>::try_make(
        usize(1024), OwnershipAllocator(state));
    EXPECT_TRUE(arena.is_err());
    EXPECT_FALSE(fail_next_runtime_allocation.load(rstd::sync::atomic::Ordering::Relaxed));
    EXPECT_EQ(state.handles, 0);
    fail_next_runtime_allocation.store(true, rstd::sync::atomic::Ordering::Relaxed);
    auto budget = rstd::alloc::BudgetAllocator<OwnershipAllocator>::try_make(
        usize(1024), OwnershipAllocator(state));
    EXPECT_TRUE(budget.is_err());
    EXPECT_FALSE(fail_next_runtime_allocation.load(rstd::sync::atomic::Ordering::Relaxed));
    EXPECT_EQ(state.handles, 0);
    fail_next_runtime_allocation.store(true, rstd::sync::atomic::Ordering::Relaxed);
    auto erased = rstd::alloc::SharedAllocator::try_make(alloc::Global {});
    EXPECT_TRUE(erased.is_err());
    EXPECT_FALSE(fail_next_runtime_allocation.load(rstd::sync::atomic::Ordering::Relaxed));
    EXPECT_EQ(state.allocations, 0);
}

TEST(AllocationOwnership, StaticBytesViewsDoNotAllocate) {
    static constexpr unsigned char data[] { 'a', 'b', 'c', 'd' };
    fail_next_runtime_allocation.store(true, rstd::sync::atomic::Ordering::Relaxed);
    {
        auto bytes  = rstd::bytes::Bytes::from_static<data>();
        auto copy   = bytes.clone();
        auto middle = copy.slice(usize(1), usize(3));
        auto prefix = middle.split_to(usize(1));
        auto suffix = bytes.split_off(usize(2));
        rstd::hint::black_box(prefix);
        rstd::hint::black_box(suffix);
    }
    EXPECT_TRUE(
        fail_next_runtime_allocation.exchange(false, rstd::sync::atomic::Ordering::Relaxed));
}

TEST(AllocationOwnership, BytesGlobalOwnerDoesNotAllocate) {
    fail_next_runtime_allocation.store(true, rstd::sync::atomic::Ordering::Relaxed);
    auto bytes  = rstd::bytes::Bytes::make();
    auto buffer = rstd::bytes::BytesMut::make();
    auto empty  = rstd::bytes::BytesMut::from_vec(Vec<u8>::make());
    auto cloned = bytes.allocator().clone();
    auto untouched =
        fail_next_runtime_allocation.exchange(false, rstd::sync::atomic::Ordering::Relaxed);
    EXPECT_TRUE(untouched);
    EXPECT_TRUE(bytes.is_empty());
    EXPECT_TRUE(buffer.is_empty());
    EXPECT_TRUE(empty.is_empty());
}

TEST(AllocationOwnership, ArcAllocatorFactoryIsNotCalledOnAllocationFailure) {
    ForwardCounts counts;
    bool          called = false;
    fail_next_runtime_allocation.store(true, rstd::sync::atomic::Ordering::Relaxed);
    auto result =
        Arc<int>::try_make_in_with(ForwardAllocator(counts), [&](ref<rstd::dyn<Allocator>>) {
            called = true;
            return 42;
        });
    auto untouched =
        fail_next_runtime_allocation.exchange(false, rstd::sync::atomic::Ordering::Relaxed);
    EXPECT_TRUE(result.is_err());
    EXPECT_FALSE(untouched);
    EXPECT_FALSE(called);
}

TEST(AllocationOwnership, GlobalPointerFailureInjectionLeavesDynInputUntouched) {
    int drops = 0;
    {
        OwnershipValue value(drops, 17);
        fail_next_runtime_allocation.store(true, rstd::sync::atomic::Ordering::Relaxed);
        auto box = Box<rstd::dyn<rstd::any::Any>>::try_make(rstd::move(value));
        EXPECT_TRUE(box.is_err());
        EXPECT_FALSE(fail_next_runtime_allocation.load(rstd::sync::atomic::Ordering::Relaxed));
        EXPECT_NE(value.drops, nullptr);
        fail_next_runtime_allocation.store(true, rstd::sync::atomic::Ordering::Relaxed);
        auto arc = Arc<rstd::dyn<rstd::any::Any>>::try_make(rstd::move(value));
        EXPECT_TRUE(arc.is_err());
        EXPECT_FALSE(fail_next_runtime_allocation.load(rstd::sync::atomic::Ordering::Relaxed));
        EXPECT_NE(value.drops, nullptr);
        fail_next_runtime_allocation.store(true, rstd::sync::atomic::Ordering::Relaxed);
        auto rc = alloc::rc::try_make_rc_in<rstd::dyn<rstd::any::Any>>(alloc::Global {},
                                                                       rstd::move(value));
        EXPECT_TRUE(rc.is_err());
        EXPECT_FALSE(fail_next_runtime_allocation.load(rstd::sync::atomic::Ordering::Relaxed));
        EXPECT_NE(value.drops, nullptr);
        EXPECT_EQ(drops, 0);
    }
    EXPECT_EQ(drops, 1);
}
#endif

struct AllocationReader {
    rstd::slice<u8> input;
    usize           position;
    usize           limit { 3 };
    int             calls {};
    int             interrupted_call { -1 };
    int             failure_call { -1 };
    bool            invalid_count {};
    bool            check_initialized {};
    bool            initialized { true };
    OwnershipState* fail_after_read {};
};

struct AllocationWriter {
    Vec<u8> output;
    int     calls {};
    bool    fail_second { true };
    bool    zero_second {};
    bool    invalid_second {};
};

struct AllocationDropReader {
    int* drops;
    explicit AllocationDropReader(int& count): drops(&count) {}
    AllocationDropReader(AllocationDropReader&& other) noexcept
        : drops(rstd::exchange(other.drops, nullptr)) {}
    ~AllocationDropReader() {
        if (drops) ++*drops;
    }
};

namespace rstd
{
template<>
struct Impl<io::Read, AllocationReader> : DefaultInImpl<io::Read, AllocationReader> {
    auto read_buf(io::BorrowedCursor buffer) -> io::Result<empty> {
        auto& reader = this->self();
        auto  call   = reader.calls++;
        if (call == reader.interrupted_call)
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Interrupted }));
        if (call == reader.failure_call)
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::PermissionDenied }));
        if (reader.invalid_count) {
            if (! buffer.advance_unchecked(buffer.capacity() + usize(1)))
                return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidData }));
        }
        if (reader.check_initialized) {
            auto target = buffer.ensure_init();
            for (usize i {}; i < target.len(); ++i)
                reader.initialized = reader.initialized && target[i] == u8();
        }
        auto count =
            min(min(reader.input.len() - reader.position, buffer.capacity()), reader.limit);
        buffer.append(slice<u8>::from_raw_parts(
            reader.input.as_raw_ptr() + reader.position.to_primitive(), count));
        reader.position += count;
        if (reader.fail_after_read) reader.fail_after_read->fail = true;
        return Ok(empty {});
    }
};
template<>
struct Impl<io::Write, AllocationWriter> : ImplBase<AllocationWriter> {
    auto write(slice<u8> input) -> io::Result<usize> {
        auto& writer = this->self();
        if (writer.calls++ == 1) {
            if (writer.zero_second) return Ok(usize());
            if (writer.invalid_second) return Ok(input.len() + usize(1));
            if (writer.fail_second)
                return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::PermissionDenied }));
        }
        auto count = min(input.len(), usize(2));
        writer.output.extend_from_slice(slice<u8>::from_raw_parts(input.as_raw_ptr(), count));
        return Ok(count);
    }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};
template<>
struct Impl<io::Read, AllocationDropReader> : DefaultInImpl<io::Read, AllocationDropReader> {
    auto read_buf(io::BorrowedCursor) -> io::Result<empty> { return Ok(empty {}); }
};
} // namespace rstd

TEST(IoAllocation, CursorWriteFailurePreservesContentAndPosition) {
    using namespace rstd::literals;
    OwnershipState state;
    {
        auto bytes = Vec<u8, OwnershipAllocator>::new_in(OwnershipAllocator(state));
        bytes.extend_from_slice("abc"_bytes);
        auto cursor = rstd::io::Cursor(rstd::move(bytes));
        cursor.set_position(u64(100));
        state.fail  = true;
        auto failed = rstd::as<rstd::io::Write>(cursor).write("z"_bytes);
        ASSERT_TRUE(failed.is_err());
        EXPECT_EQ(failed.unwrap_err().kind(),
                  rstd::io::ErrorKind { rstd::io::ErrorKind::OutOfMemory });
        EXPECT_EQ(cursor.position(), u64(100));
        EXPECT_EQ(cursor.get_ref().as_slice(), "abc"_bytes);
        state.fail = false;
        ASSERT_TRUE(rstd::as<rstd::io::Write>(cursor).write("z"_bytes).is_ok());
        EXPECT_EQ(cursor.get_ref().len(), usize(101));
        EXPECT_EQ(cursor.get_ref()[usize(99)], u8());
        EXPECT_EQ(cursor.get_ref()[usize(100)], u8('z'));
        rstd::as<rstd::io::Seek>(cursor).seek(rstd::io::SeekFrom::from_start(u64())).unwrap();
        EXPECT_EQ(rstd::as<rstd::io::BufRead>(cursor).fill_buf().unwrap().len(), usize(101));
        rstd::byte out[3] {};
        rstd::io::read_exact(cursor, rstd::mut_ref<u8[]>::from_raw_parts(out, usize(3))).unwrap();
        EXPECT_EQ(rstd::slice<u8>::from_raw_parts(out, usize(3)), "abc"_bytes);
        cursor.set_position(u64::MAX);
        EXPECT_TRUE(rstd::as<rstd::io::Write>(cursor).write("z"_bytes).is_err());
        EXPECT_EQ(cursor.position(), u64::MAX);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
    EXPECT_EQ(state.handles, 0);
}

TEST(IoAllocation, ReadToEndRetriesAndAppendsOnlyInitializedBytes) {
    using namespace rstd::literals;
    OwnershipState state;
    {
        auto bytes = Vec<u8, OwnershipAllocator>::new_in(OwnershipAllocator(state));
        bytes.extend_from_slice("pre"_bytes);
        AllocationReader reader { .input             = "abcdefghi"_bytes,
                                  .interrupted_call  = 1,
                                  .check_initialized = true };
        EXPECT_EQ(rstd::io::read_to_end(reader, bytes).unwrap(), usize(9));
        EXPECT_EQ(bytes.as_slice(), "preabcdefghi"_bytes);
        EXPECT_TRUE(reader.initialized);
        EXPECT_EQ(rstd::io::read_to_end(reader, bytes).unwrap(), usize());
        EXPECT_EQ(bytes.allocator().state, &state);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(IoAllocation, ReadToEndAllocationFailureDoesNotConsumeInput) {
    using namespace rstd::literals;
    OwnershipState state;
    {
        auto             bytes = Vec<u8, OwnershipAllocator>::new_in(OwnershipAllocator(state));
        AllocationReader reader { .input = "abcdef"_bytes, .limit = usize(4) };
        state.fail = true;
        EXPECT_TRUE(rstd::io::read_to_end(reader, bytes).is_err());
        EXPECT_EQ(reader.calls, 0);
        state.fail = false;
        bytes.try_reserve_exact(usize(4)).unwrap();
        reader.fail_after_read = &state;
        auto failed            = rstd::io::read_to_end(reader, bytes);
        ASSERT_TRUE(failed.is_err());
        EXPECT_EQ(failed.unwrap_err().kind(),
                  rstd::io::ErrorKind { rstd::io::ErrorKind::OutOfMemory });
        EXPECT_EQ(reader.position, usize(4));
        EXPECT_EQ(reader.calls, 1);
        EXPECT_EQ(bytes.as_slice(), "abcd"_bytes);
        state.fail             = false;
        reader.fail_after_read = nullptr;
        EXPECT_EQ(rstd::io::read_to_end(reader, bytes).unwrap(), usize(2));
        EXPECT_EQ(bytes.as_slice(), "abcdef"_bytes);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(IoAllocation, ReadErrorsPreservePrefixAndRejectInvalidCount) {
    using namespace rstd::literals;
    auto             bytes = Vec<u8>::from("pre"_bytes);
    AllocationReader reader { .input = "abcdef"_bytes, .failure_call = 1 };
    auto             failed = rstd::io::read_to_end(reader, bytes);
    ASSERT_TRUE(failed.is_err());
    EXPECT_EQ(failed.unwrap_err().kind(),
              rstd::io::ErrorKind { rstd::io::ErrorKind::PermissionDenied });
    EXPECT_EQ(bytes.len(), usize(3) + reader.position);
    auto before          = bytes.clone();
    reader.invalid_count = true;
    auto invalid         = rstd::io::read_to_end(reader, bytes);
    ASSERT_TRUE(invalid.is_err());
    EXPECT_EQ(invalid.unwrap_err().kind(),
              rstd::io::ErrorKind { rstd::io::ErrorKind::InvalidData });
    EXPECT_EQ(bytes.as_slice(), before.as_slice());
}

TEST(IoAllocation, FullBufferRequiresReservationEvenAtEof) {
    using namespace rstd::literals;
    OwnershipState state;
    {
        auto bytes = Vec<u8, OwnershipAllocator>::new_in(OwnershipAllocator(state));
        bytes.try_reserve_exact(usize(3)).unwrap();
        bytes.extend_from_slice("abc"_bytes);
        state.fail = true;
        AllocationReader reader { .input = ""_bytes };
        EXPECT_TRUE(rstd::io::read_to_end(reader, bytes).is_err());
        EXPECT_EQ(reader.calls, 0);
        EXPECT_EQ(bytes.as_slice(), "abc"_bytes);
    }
    EXPECT_EQ(state.live, usize());
}

TEST(IoAllocation, BufferedReaderAndDynHandleRetainAllocators) {
    using namespace rstd::literals;
    OwnershipState buffer_state, handle_state;
    {
        using Cursor      = rstd::io::Cursor<rstd::slice<u8>>;
        using Buffered    = rstd::io::BufReader<Cursor, OwnershipAllocator>;
        auto       reader = Buffered::try_make_in(
                                Cursor("abcdef"_bytes), OwnershipAllocator(buffer_state), usize(4))
                                .unwrap();
        rstd::byte value[1] {};
        auto       target = rstd::mut_ref<u8[]>::from_raw_parts(value, usize(1));
        rstd::io::read_exact(reader, target).unwrap();
        EXPECT_EQ(value[0], rstd::byte('a'));
        EXPECT_EQ(
            rstd::as<rstd::io::Seek>(reader).seek(rstd::io::SeekFrom::from_current(i64())).unwrap(),
            u64(1));
        auto handle = rstd::io::BasicReadSeekHandle<OwnershipAllocator>::make_in(
            OwnershipAllocator(handle_state), rstd::move(reader));
        EXPECT_EQ(handle->read(target).unwrap(), usize(1));
        EXPECT_EQ(value[0], rstd::byte('b'));
        EXPECT_TRUE(buffer_state.live > usize());
        EXPECT_TRUE(handle_state.live > usize());
    }
    EXPECT_EQ(buffer_state.live + handle_state.live, usize());
    EXPECT_EQ(buffer_state.invalid_releases + handle_state.invalid_releases, 0);
}

TEST(IoAllocation, BufferedCreationFailureDropsInnerAndAllocator) {
    OwnershipState state;
    int            drops {};
    state.fail   = true;
    using Reader = rstd::io::BufReader<AllocationDropReader, OwnershipAllocator>;
    EXPECT_TRUE(
        Reader::try_make_in(AllocationDropReader(drops), OwnershipAllocator(state)).is_err());
    EXPECT_EQ(drops, 1);
    EXPECT_EQ(state.handles, 0);
    EXPECT_EQ(state.live, usize());
    using Writer = rstd::io::BufWriter<rstd::io::Sink, OwnershipAllocator>;
    EXPECT_TRUE(Writer::try_make_in(rstd::io::sink(), OwnershipAllocator(state)).is_err());
    EXPECT_EQ(state.handles, 0);
    auto zero =
        Reader::try_make_in(AllocationDropReader(drops), OwnershipAllocator(state), usize());
    EXPECT_TRUE(zero.is_ok());
}

TEST(IoAllocation, BufferedWriterRetainsUnwrittenBytesOnFailure) {
    using namespace rstd::literals;
    OwnershipState state;
    {
        using Writer = rstd::io::BufWriter<AllocationWriter, OwnershipAllocator>;
        auto writer  = Writer::make_in(AllocationWriter {}, OwnershipAllocator(state), usize(8));
        rstd::as<rstd::io::Write>(writer).write("abcdef"_bytes).unwrap();
        EXPECT_TRUE(rstd::as<rstd::io::Write>(writer).flush().is_err());
        EXPECT_EQ(writer.get_ref().output.as_slice(), "ab"_bytes);
        EXPECT_EQ(writer.buffer(), "cdef"_bytes);
        writer.get_mut().fail_second = false;
        rstd::as<rstd::io::Write>(writer).flush().unwrap();
        EXPECT_EQ(writer.get_ref().output.as_slice(), "abcdef"_bytes);
        EXPECT_TRUE(writer.buffer().is_empty());
        auto inner = rstd::move(writer).into_inner();
        EXPECT_EQ(inner.output.as_slice(), "abcdef"_bytes);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(IoAllocation, FileReadInKeepsAllocatorAndDefaultUtf8Behavior) {
    using namespace rstd::literals;
    auto directory = rstd::fs::TempDir::make("rstd-allocator-io"_str).unwrap();
    auto root      = rstd::path::PathBuf::from(directory.path().as_os_str().to_os_string());
    auto path      = root.join("bytes"_str);
    rstd::fs::write(path.as_path(), "a\0\xff"_bytes).unwrap();
    OwnershipState state;
    {
        auto bytes = rstd::fs::read_in(path.as_path(), OwnershipAllocator(state)).unwrap();
        EXPECT_EQ(bytes.as_slice(), "a\0\xff"_bytes);
        EXPECT_EQ(bytes.allocator().state, &state);
        EXPECT_TRUE(rstd::fs::read_to_string(path.as_path()).is_err());
    }
    EXPECT_EQ(state.live, usize());
    state.fail  = true;
    auto failed = rstd::fs::read_in(path.as_path(), OwnershipAllocator(state));
    ASSERT_TRUE(failed.is_err());
    EXPECT_EQ(failed.unwrap_err().kind(), rstd::io::ErrorKind { rstd::io::ErrorKind::OutOfMemory });
    EXPECT_EQ(state.handles, 0);
    auto missing = root.join("missing"_str);
    auto absent  = rstd::fs::read_in(missing.as_path(), OwnershipAllocator(state));
    EXPECT_EQ(absent.unwrap_err().kind(), rstd::io::ErrorKind { rstd::io::ErrorKind::NotFound });
    state.fail = false;
    rstd::fs::write(path.as_path(), ""_bytes).unwrap();
    EXPECT_TRUE(rstd::fs::read_in(path.as_path(), OwnershipAllocator(state)).unwrap().is_empty());
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

struct AllocationCollisionHasher {
    static inline int hashes {};
    void              write(rstd::slice<u8>) noexcept { ++hashes; }
    auto              finish() const noexcept -> u64 { return u64(7); }
};
using AllocationHashBuilder = rstd::hash::BuildHasherDefault<AllocationCollisionHasher>;
template<typename V = i32>
using AllocationMap = rstd::collections::HashMap<i32,
                                                 V,
                                                 AllocationHashBuilder,
                                                 ::alloc::collections::DefaultHashEqual<i32>,
                                                 OwnershipAllocator>;
using AllocationSet = rstd::collections::HashSet<i32,
                                                 AllocationHashBuilder,
                                                 ::alloc::collections::DefaultHashEqual<i32>,
                                                 OwnershipAllocator>;

TEST(HashAllocation, ReserveFailureLeavesOriginalTableAndCanRetry) {
    OwnershipState state;
    {
        auto map   = AllocationMap<>::new_in(OwnershipAllocator(state));
        state.fail = true;
        EXPECT_TRUE(map.try_reserve(usize()).is_ok());
        EXPECT_EQ(state.allocation_attempts, 0);
        auto failed = map.try_reserve(usize(7));
        ASSERT_TRUE(failed.is_err());
        EXPECT_EQ(failed.unwrap_err(), ::alloc::collections::TryReserveError::AllocError);
        EXPECT_EQ(map.capacity(), usize());
        state.fail = false;
        map.try_reserve(usize(7)).unwrap();
        for (int i = 0; i < 7; ++i) map.insert(i32(i), i32(i * 2));
        auto capacity = map.capacity();
        auto live     = state.live;
        state.fail    = true;
        failed        = map.try_reserve(usize(100));
        EXPECT_TRUE(failed.is_err());
        EXPECT_EQ(map.len(), usize(7));
        EXPECT_EQ(map.capacity(), capacity);
        EXPECT_EQ(state.live, live);
        EXPECT_EQ(map.allocator().state, &state);
        for (int i = 0; i < 7; ++i) EXPECT_EQ(map.get(i32(i)).unwrap().get(), i32(i * 2));
        auto attempts = state.allocation_attempts;
        EXPECT_EQ(map.insert(i32(3), i32(33)).unwrap(), i32(6));
        EXPECT_EQ(state.allocation_attempts, attempts);
        state.fail                        = false;
        AllocationCollisionHasher::hashes = 0;
        map.try_reserve(usize(100)).unwrap();
        EXPECT_EQ(AllocationCollisionHasher::hashes, 0);
        EXPECT_TRUE(map.capacity() >= usize(107));
        attempts   = state.allocation_attempts;
        state.fail = true;
        for (int i = 7; i < 107; ++i) map.insert(i32(i), i32(i));
        EXPECT_EQ(state.allocation_attempts, attempts);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
    EXPECT_EQ(state.handles, 0);
}

TEST(HashAllocation, TombstoneCleanupIsTransactionalAndHonorsReservation) {
    OwnershipState state;
    {
        auto map = AllocationMap<>::new_in(OwnershipAllocator(state));
        map.try_reserve(usize(7)).unwrap();
        for (int i = 0; i < 7; ++i) map.insert(i32(i), i32(i));
        for (int i = 0; i < 6; ++i) map.remove(i32(i));
        auto capacity = map.capacity();
        state.fail    = true;
        auto attempts = state.allocation_attempts;
        EXPECT_TRUE(map.try_reserve(usize()).is_ok());
        EXPECT_EQ(state.allocation_attempts, attempts);
        EXPECT_TRUE(map.try_reserve(usize(6)).is_err());
        EXPECT_EQ(map.len(), usize(1));
        EXPECT_EQ(map.capacity(), capacity);
        EXPECT_EQ(map.get(i32(6)).unwrap().get(), i32(6));
        for (int i = 0; i < 6; ++i) EXPECT_TRUE(map.get(i32(i)).is_none());
        state.fail = false;
        map.try_reserve(usize(6)).unwrap();
        EXPECT_EQ(map.capacity(), capacity);
        attempts   = state.allocation_attempts;
        state.fail = true;
        for (int i = 10; i < 16; ++i) map.insert(i32(i), i32(i));
        EXPECT_EQ(map.len(), usize(7));
        EXPECT_EQ(state.allocation_attempts, attempts);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(HashAllocation, OverflowNeverCallsAllocatorOrMovesValues) {
    OwnershipState state;
    int            drops {};
    {
        auto  map      = AllocationMap<OwnershipValue>::new_in(OwnershipAllocator(state));
        auto  attempts = state.allocation_attempts;
        usize counts[] { usize::MAX, usize::MAX / usize(2), usize::MAX / usize(32) };
        for (auto count : counts) {
            auto failed = map.try_reserve(count);
            ASSERT_TRUE(failed.is_err());
            EXPECT_EQ(failed.unwrap_err(), ::alloc::collections::TryReserveError::CapacityOverflow);
        }
        EXPECT_EQ(state.allocation_attempts, attempts);
        map.insert(i32(1), OwnershipValue(drops, 42));
        attempts = state.allocation_attempts;
        EXPECT_EQ(map.try_reserve(usize::MAX).unwrap_err(),
                  ::alloc::collections::TryReserveError::CapacityOverflow);
        EXPECT_EQ(state.allocation_attempts, attempts);
        state.fail = true;
        EXPECT_TRUE(map.try_reserve(usize(100)).is_err());
        EXPECT_EQ(drops, 0);
        EXPECT_EQ(map.get(i32(1)).unwrap()->value, 42);
        state.fail = false;
        map.try_reserve(usize(100)).unwrap();
        EXPECT_EQ(drops, 0);
        EXPECT_EQ(map.get(i32(1)).unwrap()->value, 42);
    }
    EXPECT_EQ(drops, 1);
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(HashAllocation, HashSetUsesTheSameFallibleStorage) {
    OwnershipState state;
    {
        auto set   = AllocationSet::new_in(OwnershipAllocator(state));
        state.fail = true;
        EXPECT_TRUE(set.try_reserve(usize(20)).is_err());
        EXPECT_TRUE(set.is_empty());
        state.fail = false;
        set.try_reserve(usize(20)).unwrap();
        auto attempts = state.allocation_attempts;
        state.fail    = true;
        for (int i = 0; i < 20; ++i) EXPECT_TRUE(set.insert(i32(i)));
        EXPECT_FALSE(set.insert(i32(1)));
        EXPECT_EQ(state.allocation_attempts, attempts);
        EXPECT_TRUE(set.try_reserve(usize(100)).is_err());
        for (int i = 0; i < 20; ++i) EXPECT_TRUE(set.contains(i32(i)));
        EXPECT_EQ(set.allocator().state, &state);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(IoAllocation, BufferedWriterRemovesWrittenPrefixOnZeroOrInvalidCount) {
    using namespace rstd::literals;
    for (int mode = 0; mode < 2; ++mode) {
        auto writer = rstd::io::BufWriter(
            AllocationWriter { .zero_second = mode == 0, .invalid_second = mode == 1 }, usize(8));
        rstd::io::write_all(writer, "abcdef"_bytes).unwrap();
        EXPECT_TRUE(rstd::as<rstd::io::Write>(writer).flush().is_err());
        EXPECT_EQ(writer.buffer(), "cdef"_bytes);
        rstd::as<rstd::io::Write>(writer).flush().unwrap();
        EXPECT_EQ(writer.get_ref().output.as_slice(), "abcdef"_bytes);
    }
}

TEST(IoAllocation, ZeroCapacityBuffersAndSeekableHandlesKeepOwnership) {
    using namespace rstd::literals;
    OwnershipState state;
    {
        state.fail   = true;
        using Cursor = rstd::io::Cursor<rstd::slice<u8>>;
        auto reader  = rstd::io::BufReader<Cursor, OwnershipAllocator>::make_in(
            Cursor("abc"_bytes), OwnershipAllocator(state), usize());
        rstd::byte bytes[3] {};
        rstd::io::read_exact(reader, rstd::mut_ref<u8[]>::from_raw_parts(bytes, usize(3))).unwrap();
        EXPECT_EQ(bytes[2], rstd::byte('c'));
        EXPECT_EQ(
            rstd::as<rstd::io::Seek>(reader).seek(rstd::io::SeekFrom::from_start(u64())).unwrap(),
            u64());
        auto inner = rstd::move(reader).into_inner();
        EXPECT_EQ(inner.position(), u64());
        auto zero = rstd::io::BufWriter<rstd::io::Sink, OwnershipAllocator>::make_in(
            rstd::io::sink(), OwnershipAllocator(state), usize());
        rstd::io::write_all(zero, "abc"_bytes).unwrap();
        rstd::as<rstd::io::Write>(zero).flush().unwrap();
        EXPECT_EQ(state.allocation_attempts, 0);
        state.fail  = false;
        using Owned = rstd::io::Cursor<Vec<u8, OwnershipAllocator>>;
        auto owned  = Owned(Vec<u8, OwnershipAllocator>::new_in(OwnershipAllocator(state)));
        auto writer = rstd::io::BufWriter<Owned, OwnershipAllocator>::make_in(
            rstd::move(owned), OwnershipAllocator(state), usize(4));
        auto handle = rstd::io::BasicWriteSeekHandle<OwnershipAllocator>::make_in(
            OwnershipAllocator(state), rstd::move(writer));
        handle->write("abc"_bytes).unwrap();
        EXPECT_EQ(handle->seek(rstd::io::SeekFrom::from_current(i64())).unwrap(), u64(3));
        auto both = rstd::io::BasicReadWriteSeekHandle<OwnershipAllocator>::make_in(
            OwnershipAllocator(state),
            Owned(Vec<u8, OwnershipAllocator>::new_in(OwnershipAllocator(state))));
        both->write("xyz"_bytes).unwrap();
        both->seek(rstd::io::SeekFrom::from_start(u64())).unwrap();
        both->read(rstd::mut_ref<u8[]>::from_raw_parts(bytes, usize(3))).unwrap();
        EXPECT_EQ(bytes[2], rstd::byte('z'));
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.handles, 0);
    EXPECT_EQ(state.invalid_releases, 0);
}

struct AllocationReadAt {
    rstd::slice<u8> bytes;
};
template<>
struct rstd::Impl<rstd::io::ReadAt, AllocationReadAt>
    : rstd::DefaultInImpl<rstd::io::ReadAt, AllocationReadAt> {
    auto read_buf_at(rstd::io::BorrowedCursor target, u64 offset) const -> rstd::io::Result<empty> {
        const auto bytes = this->self().bytes;
        if (offset >= u64(bytes.len().to_primitive())) return Ok(empty {});
        auto start = usize(offset.to_primitive());
        auto count = rstd::min(target.capacity(), bytes.len() - start);
        target.append(
            rstd::slice<u8>::from_raw_parts(bytes.as_raw_ptr() + start.to_primitive(), count));
        return Ok(empty {});
    }
};
TEST(IoAllocation, RangeBufferAndDynHandleOwnIndependentAllocationDomains) {
    using namespace rstd::literals;
    auto           root = rstd::alloc::SharedTrackingAllocator<>::make();
    OwnershipState state;
    {
        auto source =
            rstd::io::SharedReadAt::make_in(root.clone(), AllocationReadAt { "abcdef"_bytes });
        auto range  = rstd::io::ReadRange::make(rstd::move(source), u64(1), u64(4)).unwrap();
        auto reader = rstd::io::BufReader<rstd::io::RangeReader, OwnershipAllocator>::make_in(
            rstd::move(range).into_reader(), OwnershipAllocator(state), usize(2));
        auto handle = rstd::io::BasicReadSeekHandle<OwnershipAllocator>::make_in(
            OwnershipAllocator(state), rstd::move(reader));
        rstd::byte bytes[4] {};
        EXPECT_EQ(handle->read(rstd::mut_ref<u8[]>::from_raw_parts(bytes, usize(4))).unwrap(),
                  usize(4));
        EXPECT_EQ(bytes[0], rstd::byte('b'));
        EXPECT_EQ(bytes[3], rstd::byte('e'));
    }
    EXPECT_EQ(root.snapshot().live_bytes, u64());
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.handles, 0);
    EXPECT_EQ(state.invalid_releases, 0);
}

#if defined(__cpp_exceptions)
struct HashMoveState {
    int live {};
    int moves {};
    int throw_at {};
};
struct HashMoveValue {
    HashMoveState* state;
    i32            id;
    HashMoveValue(HashMoveState& state, i32 id): state(&state), id(id) { ++state.live; }
    HashMoveValue(const HashMoveValue&) = delete;
    HashMoveValue(HashMoveValue&& other): state(other.state), id(other.id) {
        if (++state->moves == state->throw_at) throw 41;
        ++state->live;
    }
    ~HashMoveValue() { --state->live; }
    friend bool operator==(const HashMoveValue& a, const HashMoveValue& b) { return a.id == b.id; }
};
template<>
struct rstd::Impl<rstd::hash::Hash, HashMoveValue> : rstd::ImplBase<HashMoveValue> {
    template<typename H>
    void hash(H& state) const noexcept {
        rstd::hash::write_value(state, this->self().id);
    }
};
TEST(HashAllocation, ThrowingMovesReleaseBothStorageAndPartiallyConstructedBuckets) {
    // Each attempt throws at a different move, including value construction after the key.
    for (int throw_at = 1; throw_at <= 32; ++throw_at) {
        OwnershipState allocator;
        HashMoveState  keys, values;
        {
            using Map =
                rstd::collections::HashMap<HashMoveValue,
                                           HashMoveValue,
                                           AllocationHashBuilder,
                                           ::alloc::collections::DefaultHashEqual<HashMoveValue>,
                                           OwnershipAllocator>;
            auto map = Map::new_in(OwnershipAllocator(allocator));
            for (int i = 0; i < 3; ++i)
                map.insert(HashMoveValue(keys, i32(i)), HashMoveValue(values, i32(i)));
            values.moves    = 0;
            values.throw_at = throw_at;
            try {
                map.try_reserve(usize(100)).unwrap();
            } catch (int error) {
                EXPECT_EQ(error, 41);
            }
            EXPECT_EQ(keys.live, int(map.len().to_primitive()));
            EXPECT_EQ(values.live, int(map.len().to_primitive()));
            values.throw_at = 0;
            map.clear();
            map.insert(HashMoveValue(keys, i32(9)), HashMoveValue(values, i32(9)));
            EXPECT_EQ(map.len(), usize(1));
        }
        EXPECT_EQ(keys.live, 0);
        EXPECT_EQ(values.live, 0);
        EXPECT_EQ(allocator.live, usize());
        EXPECT_EQ(allocator.invalid_releases, 0);
    }
}
#endif

TEST(AllocationOwnership, UninitBoxPreservesBytesLayoutAndAllocator) {
    OwnershipState state;
    {
        auto box = Box<OwnershipValue[], OwnershipAllocator>::try_make_uninit_in(
                       usize(3), OwnershipAllocator(state))
                       .unwrap();
        EXPECT_EQ(box.as_ptr().len(), usize(3));
        EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(box.as_ptr().as_raw_ptr()) % 256, 0u);
        auto representation = reinterpret_cast<const rstd::byte*>(box.as_ptr().as_raw_ptr());
        EXPECT_EQ(representation[0], rstd::byte(0xcd));
        EXPECT_EQ(representation[sizeof(OwnershipValue) * 3 - 1], rstd::byte(0xcd));
        auto moved = rstd::move(box);
        EXPECT_EQ(moved.allocator().state, &state);
        int  drops = 0;
        auto slots = moved.as_mut_ptr().as_raw_ptr();
        slots[0].write(OwnershipValue(drops, 7));
        EXPECT_EQ(slots[0].assume_init_ref().value, 7);
        slots[0].assume_init_drop();
        EXPECT_EQ(drops, 1);
    }
    EXPECT_EQ(state.allocations, 1);
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.handles, 0);
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, UninitBoxEmptyOverflowAndFailureReleaseAllocator) {
    OwnershipState state;
    using Bytes = Box<rstd::byte[], OwnershipAllocator>;
    {
        auto box = Bytes::make_uninit_in(usize(), OwnershipAllocator(state));
        EXPECT_EQ(box.as_ptr().len(), usize());
        EXPECT_EQ(state.allocation_attempts, 0);
    }
    EXPECT_EQ(state.handles, 0);
    auto overflow = Bytes::try_make_uninit_in(usize::MAX, OwnershipAllocator(state));
    ASSERT_TRUE(overflow.is_err());
    EXPECT_EQ(overflow.unwrap_err(), alloc::collections::TryReserveError::CapacityOverflow);
    EXPECT_EQ(state.allocation_attempts, 0);
    state.fail   = true;
    auto failure = Bytes::try_make_uninit_in(usize(16), OwnershipAllocator(state));
    ASSERT_TRUE(failure.is_err());
    EXPECT_EQ(failure.unwrap_err(), alloc::collections::TryReserveError::AllocError);
    EXPECT_EQ(state.handles, 0);
}

TEST(IoAllocation, StaticCursorReservesOnceAndRollsBackOnFailure) {
    using namespace rstd::literals;
    OwnershipState state;
    {
        auto output = Vec<u8, OwnershipAllocator>::new_in(OwnershipAllocator(state));
        auto reader = rstd::io::Cursor("abcdef"_bytes);
        reader.set_position(u64(1));
        state.fail = true;
        EXPECT_TRUE(rstd::io::read_to_end(reader, output).is_err());
        EXPECT_EQ(reader.position(), u64(1));
        EXPECT_TRUE(output.is_empty());
        state.fail = false;
        EXPECT_EQ(rstd::io::read_to_end(reader, output).unwrap(), usize(5));
        EXPECT_EQ(output.as_slice(), "bcdef"_bytes);
        EXPECT_EQ(state.allocations, 1);
        auto attempts = state.allocation_attempts;
        state.fail    = true;
        EXPECT_EQ(rstd::io::read_to_end(reader, output).unwrap(), usize());
        EXPECT_EQ(state.allocation_attempts, attempts);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
    EXPECT_EQ(state.handles, 0);
}

TEST(IoAllocation, EmptyCursorDoesNotAllocateAndBufferedDrainDoesNotConsumeOnOom) {
    using namespace rstd::literals;
    OwnershipState state;
    {
        auto output = Vec<u8, OwnershipAllocator>::new_in(OwnershipAllocator(state));
        state.fail  = true;
        auto empty  = rstd::io::Cursor(""_bytes);
        EXPECT_EQ(rstd::io::read_to_end(empty, output).unwrap(), usize());
        EXPECT_EQ(state.allocation_attempts, 0);
        auto reader = rstd::io::BufReader(rstd::io::Cursor("abcdef"_bytes), usize(4));
        EXPECT_EQ(rstd::as<rstd::io::BufRead>(reader).fill_buf().unwrap(), "abcd"_bytes);
        EXPECT_TRUE(rstd::io::read_to_end(reader, output).is_err());
        EXPECT_EQ(reader.buffer(), "abcd"_bytes);
        EXPECT_EQ(reader.get_ref().position(), u64(4));
        state.fail = false;
        EXPECT_EQ(rstd::io::read_to_end(reader, output).unwrap(), usize(6));
        EXPECT_EQ(output.as_slice(), "abcdef"_bytes);
    }
    EXPECT_EQ(state.live, usize());
}

TEST(IoAllocation, BufReaderStorageStartsUninitializedAndNeverGrows) {
    using namespace rstd::literals;
    OwnershipState state;
    {
        using Reader = rstd::io::BufReader<rstd::io::Cursor<rstd::slice<u8>>, OwnershipAllocator>;
        auto reader  = Reader::make_in(
            rstd::io::Cursor("abcdefgh"_bytes), OwnershipAllocator(state), usize(4));
        EXPECT_EQ(state.allocations, 1);
        auto pointer = static_cast<const rstd::byte*>(state.blocks[0].pointer);
        EXPECT_EQ(pointer[0], rstd::byte(0xcd));
        EXPECT_EQ(pointer[3], rstd::byte(0xcd));
        state.fail = true;
        EXPECT_EQ(rstd::as<rstd::io::BufRead>(reader).fill_buf().unwrap(), "abcd"_bytes);
        rstd::as<rstd::io::BufRead>(reader).consume(usize(4));
        EXPECT_EQ(rstd::as<rstd::io::BufRead>(reader).fill_buf().unwrap(), "efgh"_bytes);
        EXPECT_EQ(state.allocation_attempts, 1);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, BytesRetainSharedAllocatorAndOriginalLayout) {
    OwnershipState state;
    state.extra = usize(32);
    {
        auto allocator = SharedOwner::make(OwnershipAllocator(state));
        auto vec       = Vec<u8, SharedOwner>::with_capacity_in(usize(8), allocator.clone());
        vec.push(u8(1));
        vec.push(u8(2));
        auto* storage = vec.data();
        auto  bytes   = rstd::bytes::Bytes::from_vec(rstd::move(vec));
        EXPECT_EQ(bytes.data(), storage);
        EXPECT_EQ(bytes.capacity(), usize(8));
        auto buffer = rstd::bytes::BytesMut::with_capacity_in(usize(8), bytes.allocator());
        buffer.put_slice(bytes.as_slice());
        auto prefix = buffer.split_to(usize(1));
        auto frozen = buffer.freeze();
        EXPECT_EQ(prefix[usize()], u8(1));
        EXPECT_EQ(frozen[usize()], u8(2));
        EXPECT_EQ(state.handles, 1);
    }
    EXPECT_EQ(state.handles, 0);
    EXPECT_EQ(state.releases, 1);
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.allocations, state.deallocations);
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, BytesSplitSharesNonCloneableAllocator) {
    ForwardCounts counts;
    static_assert(! rstd::alloc::CloneableAllocator<ForwardAllocator>);
    {
        auto vec = Vec<u8, ForwardAllocator>::with_capacity_in(usize(8), ForwardAllocator(counts));
        vec.push(u8(1));
        vec.push(u8(2));
        auto buffer = rstd::bytes::BytesMut::from_vec(rstd::move(vec));
        auto prefix = buffer.split_to(usize(1));
        EXPECT_EQ(prefix[usize()], u8(1));
        auto frozen = buffer.freeze();
        EXPECT_EQ(frozen[usize()], u8(2));
        EXPECT_EQ(counts.calls[0].load(rstd::sync::atomic::Ordering::Relaxed), usize(4));
    }
    EXPECT_EQ(counts.calls[2].load(rstd::sync::atomic::Ordering::Relaxed), usize(4));
}

TEST(AllocationOwnership, ArcAllocatorOutlivesValueAndWeakWithoutRetainingValue) {
    OwnershipState state;
    int            drops = 0;
    {
        auto value =
            Arc<OwnershipValue>::make_in(SharedOwner::make(OwnershipAllocator(state)), drops, 7);
        auto weak      = value.downgrade();
        auto allocator = value.allocator_owner();
        value.reset();
        EXPECT_EQ(drops, 1);
        EXPECT_TRUE(weak.expired());
        weak.reset();
        EXPECT_EQ(state.releases, 0);
        auto box = Box<int, rstd::alloc::ArcAllocator>::make_in(allocator.clone(), 42);
        EXPECT_EQ(*box, 42);
    }
    EXPECT_EQ(drops, 1);
    EXPECT_EQ(state.releases, 1);
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
}

TEST(AllocationOwnership, ArcAllocatorFactoryBorrowStaysBoundToOwner) {
    OwnershipState state;
    using Buffer = Vec<u8, ref<rstd::dyn<Allocator>>>;
    {
        auto storage = Arc<Buffer>::make_in_with(
            SharedOwner::make(OwnershipAllocator(state)), [](ref<rstd::dyn<Allocator>> allocator) {
                auto buffer = Buffer::with_capacity_in(usize(8), allocator);
                buffer.push(u8(42));
                return buffer;
            });
        auto extracted = storage.try_unwrap();
        ASSERT_TRUE(extracted.is_err());
        auto retained = rstd::move(extracted).unwrap_err_unchecked();
        EXPECT_EQ((*retained)[usize()], u8(42));
        EXPECT_EQ(state.allocations, 2);
    }
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.invalid_releases, 0);
    EXPECT_EQ(state.releases, 1);
}

TEST(AllocationOwnership, BytesTypedConversionPreservesOriginalLayoutAndRelease) {
    OwnershipState state;
    state.extra = usize(32);
    {
        auto allocator = SharedOwner::make(OwnershipAllocator(state));
        auto values    = Vec<u32, SharedOwner>::with_capacity_in(usize(8), allocator.clone());
        values.push(u32(0x12345678));
        auto* pointer = reinterpret_cast<const byte*>(values.data());
        auto  before  = state.allocations;
        auto  raw     = rstd::move(values).into_bytes();
        EXPECT_EQ(state.allocations, before);
        EXPECT_EQ(raw.capacity(), usize(8 * sizeof(u32)));
        auto bytes = rstd::bytes::Bytes::from_vec(rstd::move(raw));
        EXPECT_EQ(bytes.data(), pointer);
        EXPECT_EQ(state.allocations, before + 1);
        auto snapshot = bytes.clone();
        auto writable = rstd::move(bytes).into_mut();
        EXPECT_EQ(writable.alignment(), usize(alignof(u32)));
        writable.reserve(usize(128));
        EXPECT_EQ(writable.alignment(), usize(alignof(u32)));
        EXPECT_EQ(state.invalid_releases, 0);
    }
    EXPECT_EQ(state.allocations, state.deallocations);
    EXPECT_EQ(state.invalid_releases, 0);
    EXPECT_EQ(state.live, usize());
    EXPECT_EQ(state.releases, 1);
}

TEST(AllocationOwnership, ByteVecNormalizesLayoutWhenTransferredToBox) {
    OwnershipState state;
    {
        auto values =
            Vec<u32, OwnershipAllocator>::with_capacity_in(usize(4), OwnershipAllocator(state));
        values.push(u32(0x12345678));
        auto bytes = rstd::move(values).into_bytes();
        bytes.shrink_to_fit();
        EXPECT_EQ(bytes.allocation_alignment(), usize(alignof(u32)));
        state.fail  = true;
        auto failed = bytes.try_into_boxed_slice();
        ASSERT_TRUE(failed.is_err());
        auto restored = rstd::move(failed).unwrap_err();
        EXPECT_EQ(restored.allocation_alignment(), usize(alignof(u32)));
        state.fail = false;
        auto boxed = restored.try_into_boxed_slice().unwrap();
        EXPECT_EQ(boxed.as_ptr().len(), usize(sizeof(u32)));
    }
    EXPECT_EQ(state.invalid_releases, 0);
    EXPECT_EQ(state.allocations, state.deallocations);
    EXPECT_EQ(state.live, usize());
}
