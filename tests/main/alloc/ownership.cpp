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
