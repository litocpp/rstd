#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

constexpr bool swap_proxy_handles() {
    rstd::byte data[] = { rstd::byte { 3 }, rstd::byte { 1 } };
    auto       a      = mut_ref<u8>::from_raw_parts(data);
    auto       b      = mut_ref<u8>::from_raw_parts(data + 1);
    rstd::mem::swap(a, b);
    rstd::mem::swap(a, a);
    if (a.as_raw_ptr() != data + 1 || b.as_raw_ptr() != data) return false;
    if (data[0] != rstd::byte { 3 } || data[1] != rstd::byte { 1 }) return false;
    a = b;
    return a.as_raw_ptr() == data + 1 && data[1] == rstd::byte { 3 };
}
static_assert(swap_proxy_handles());

TEST(Mem, SwapProxyHandlesPreservesWriteThroughAssignment) {
    EXPECT_TRUE(swap_proxy_handles());
}

struct SwapLifetime {
    int constructed = 0;
    int destroyed   = 0;
};

struct SwapValue {
    int           value;
    SwapLifetime* counts;
    SwapValue(int value, SwapLifetime& counts): value(value), counts(&counts) {
        ++counts.constructed;
    }
    SwapValue(SwapValue&& other) noexcept: value(other.value), counts(other.counts) {
        ++counts->constructed;
    }
    SwapValue(const SwapValue&)               = delete;
    auto operator=(SwapValue&&) -> SwapValue& = delete;
    ~SwapValue() { ++counts->destroyed; }
};

TEST(Mem, SwapNonAssignableObjectsAndSelf) {
    SwapLifetime counts;
    {
        SwapValue a(1, counts), b(2, counts);
        rstd::mem::swap(a, b);
        EXPECT_EQ(a.value, 2);
        EXPECT_EQ(b.value, 1);
        EXPECT_EQ(counts.constructed - counts.destroyed, 2);
        const auto constructed = counts.constructed;
        const auto destroyed   = counts.destroyed;
        rstd::mem::swap(a, a);
        EXPECT_EQ(counts.constructed, constructed);
        EXPECT_EQ(counts.destroyed, destroyed);
    }
    EXPECT_EQ(counts.constructed, counts.destroyed);
}

using rstd::mem::MaybeUninit;

struct alignas(64) UninitValue {
    SwapLifetime* counts;
    const int     value;
    UninitValue() = delete;
    UninitValue(SwapLifetime& counts, int value): counts(&counts), value(value) {
        ++counts.constructed;
    }
    UninitValue(UninitValue&& other) noexcept: counts(other.counts), value(other.value) {
        ++counts->constructed;
    }
    UninitValue(const UninitValue&) = delete;
    ~UninitValue() { ++counts->destroyed; }
};

static_assert(sizeof(MaybeUninit<UninitValue>) == sizeof(UninitValue));
static_assert(alignof(MaybeUninit<UninitValue>) == alignof(UninitValue));
static_assert(__is_trivially_destructible(MaybeUninit<UninitValue>));
static_assert(! __is_trivially_constructible(MaybeUninit<int>));

TEST(MaybeUninit, PlacementConstructionPreservesStorage) {
    using Slot = MaybeUninit<UninitValue>;
    for (int form = 0; form < 3; ++form) {
        alignas(Slot) rstd::byte bytes[sizeof(Slot)];
        __builtin_memset(bytes, 0xa5, sizeof(bytes));
        Slot* slot;
        switch (form) {
        case 0: slot = ::new (static_cast<void*>(bytes)) Slot; break;
        case 1: slot = ::new (static_cast<void*>(bytes)) Slot(); break;
        default: slot = ::new (static_cast<void*>(bytes)) Slot {}; break;
        }
        auto representation = reinterpret_cast<const rstd::byte*>(slot);
        for (rstd::size_t i = 0; i < sizeof(Slot); ++i)
            EXPECT_EQ(representation[i], rstd::byte(0xa5));
        EXPECT_EQ(static_cast<void*>(slot->as_mut_ptr()), static_cast<void*>(slot));
        rstd::destroy_at(slot);
    }
}

TEST(MaybeUninit, PlacementArrayConstructionPreservesStorage) {
    using Slot = MaybeUninit<int>;
    for (int form = 0; form < 3; ++form) {
        alignas(Slot) rstd::byte bytes[sizeof(Slot) * 3];
        __builtin_memset(bytes, 0x5a, sizeof(bytes));
        Slot* slots;
        switch (form) {
        case 0: slots = ::new (static_cast<void*>(bytes)) Slot[3]; break;
        case 1: slots = ::new (static_cast<void*>(bytes)) Slot[3](); break;
        default: slots = ::new (static_cast<void*>(bytes)) Slot[3] {}; break;
        }
        auto representation = reinterpret_cast<const rstd::byte*>(slots);
        for (rstd::size_t i = 0; i < sizeof(bytes); ++i)
            EXPECT_EQ(representation[i], rstd::byte(0x5a));
        for (int i = 0; i < 3; ++i) {
            slots[i].write(40 + i);
            EXPECT_EQ(slots[i].assume_init_read(), 40 + i);
            slots[i].assume_init_drop();
            rstd::destroy_at(slots + i);
        }
    }
}

TEST(MaybeUninit, ZeroedClearsStorageWithoutConstructingValue) {
    using Slot = MaybeUninit<UninitValue>;
    alignas(Slot) rstd::byte bytes[sizeof(Slot)];
    __builtin_memset(bytes, 0xa5, sizeof(bytes));
    auto slot           = ::new (static_cast<void*>(bytes)) Slot(Slot::zeroed());
    auto representation = reinterpret_cast<const rstd::byte*>(slot);
    for (rstd::size_t i = 0; i < sizeof(Slot); ++i) EXPECT_EQ(representation[i], rstd::byte());
    rstd::destroy_at(slot);
}

TEST(MaybeUninit, HeapConstructionAndExplicitLifetime) {
    using Slot = MaybeUninit<UninitValue>;
    for (int form = 0; form < 4; ++form) {
        SwapLifetime counts;
        Slot*        slot;
        switch (form) {
        case 0: slot = new Slot; break;
        case 1: slot = new Slot(); break;
        case 2: slot = new Slot {}; break;
        default: slot = new Slot(Slot::uninit()); break;
        }
        EXPECT_EQ(counts.constructed, 0);
        EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(slot->as_mut_ptr()) % alignof(UninitValue), 0u);
        auto value = rstd::construct_at(slot->as_mut_ptr(), counts, 42);
        EXPECT_EQ(&slot->assume_init_ref(), value);
        EXPECT_EQ(slot->assume_init_ref().value, 42);
        slot->assume_init_drop();
        EXPECT_EQ(counts.destroyed, 1);
        slot->write(UninitValue(counts, 73));
        EXPECT_EQ(slot->assume_init_mut().value, 73);
        slot->assume_init_drop();
        EXPECT_EQ(counts.constructed, counts.destroyed);
        auto destroyed = counts.destroyed;
        delete slot;
        EXPECT_EQ(counts.destroyed, destroyed);
    }
}

TEST(MaybeUninit, HeapArraysConstructOnlyWrittenValues) {
    using Slot = MaybeUninit<UninitValue>;
    for (int form = 0; form < 3; ++form) {
        SwapLifetime counts;
        Slot*        slots;
        switch (form) {
        case 0: slots = new Slot[3]; break;
        case 1: slots = new Slot[3](); break;
        default: slots = new Slot[3] {}; break;
        }
        rstd::construct_at(slots[1].as_mut_ptr(), counts, 42);
        EXPECT_EQ(counts.constructed, 1);
        EXPECT_EQ(slots[1].assume_init_ref().value, 42);
        slots[1].assume_init_drop();
        delete[] slots;
        EXPECT_EQ(counts.destroyed, 1);
    }
}

TEST(MaybeUninit, MakeConstructsValueAndRequiresExplicitDrop) {
    SwapLifetime counts;
    {
        auto slot = MaybeUninit<UninitValue>::make(UninitValue(counts, 42));
        EXPECT_EQ(slot.assume_init_ref().value, 42);
        EXPECT_EQ(counts.constructed - counts.destroyed, 1);
        slot.assume_init_drop();
    }
    EXPECT_EQ(counts.constructed, counts.destroyed);
}

template<typename T>
concept RvalueUninitRef = requires(T&& value) { rstd::move(value).assume_init_ref(); };
template<typename T>
concept RvalueUninitMut = requires(T&& value) { rstd::move(value).assume_init_mut(); };
static_assert(! RvalueUninitRef<MaybeUninit<int>>);
static_assert(! RvalueUninitMut<MaybeUninit<int>>);
static_assert(noexcept(rstd::mtp::declval<MaybeUninit<UninitValue>&&>().assume_init()));

TEST(MaybeUninit, AssumeInitConsumesAndAllowsReconstruction) {
    SwapLifetime             counts;
    MaybeUninit<UninitValue> slot;
    rstd::construct_at(slot.as_mut_ptr(), counts, 42);
    {
        auto value = rstd::move(slot).assume_init();
        EXPECT_EQ(value.value, 42);
        EXPECT_EQ(counts.constructed, 2);
        EXPECT_EQ(counts.destroyed, 1);
        rstd::construct_at(slot.as_mut_ptr(), counts, 73);
        EXPECT_EQ(slot.assume_init_ref().value, 73);
        slot.assume_init_drop();
    }
    EXPECT_EQ(counts.constructed, counts.destroyed);
}

struct UninitCopyValue {
    SwapLifetime* counts;
    int           value;
    UninitCopyValue(SwapLifetime& counts, int value): counts(&counts), value(value) {
        ++counts.constructed;
    }
    UninitCopyValue(const UninitCopyValue& other) noexcept
        : counts(other.counts), value(other.value) {
        ++counts->constructed;
    }
    ~UninitCopyValue() { ++counts->destroyed; }
};

TEST(MaybeUninit, AssumeReadCopiesWhileBorrowsPreserveIdentity) {
    SwapLifetime                 counts;
    MaybeUninit<UninitCopyValue> slot;
    auto                         original = rstd::construct_at(slot.as_mut_ptr(), counts, 42);
    EXPECT_EQ(&slot.assume_init_ref(), original);
    EXPECT_EQ(&slot.assume_init_mut(), original);
    slot.assume_init_mut().value = 73;
    {
        auto copy = slot.assume_init_read();
        EXPECT_EQ(copy.value, 73);
        EXPECT_EQ(counts.constructed, 2);
        EXPECT_EQ(counts.destroyed, 0);
        copy.value = 99;
        EXPECT_EQ(slot.assume_init_ref().value, 73);
    }
    EXPECT_EQ(counts.destroyed, 1);
    slot.assume_init_drop();
    EXPECT_EQ(counts.constructed, counts.destroyed);
}

#if defined(__cpp_exceptions)
struct UninitThrowState {
    int live    = 0;
    int moves   = 0;
    int fail_at = 0;
};

struct UninitThrowValue {
    UninitThrowState* state;
    explicit UninitThrowValue(UninitThrowState& state): state(&state) { ++state.live; }
    UninitThrowValue(UninitThrowValue&& other): state(other.state) {
        if (++state->moves == state->fail_at) throw 42;
        ++state->live;
    }
    UninitThrowValue(const UninitThrowValue&) { throw 73; }
    ~UninitThrowValue() { --state->live; }
};
static_assert(! noexcept(rstd::mtp::declval<MaybeUninit<UninitThrowValue>&&>().assume_init()));
static_assert(
    ! noexcept(rstd::mtp::declval<const MaybeUninit<UninitThrowValue>&>().assume_init_read()));

TEST(MaybeUninit, ThrowingMoveConsumesButThrowingCopyPreservesSource) {
    UninitThrowState              state;
    MaybeUninit<UninitThrowValue> slot;
    rstd::construct_at(slot.as_mut_ptr(), state);
    bool copy_threw = false;
    try {
        auto value = slot.assume_init_read();
    } catch (int error) {
        copy_threw = true;
        EXPECT_EQ(error, 73);
    }
    EXPECT_TRUE(copy_threw);
    EXPECT_EQ(state.live, 1);
    EXPECT_EQ(slot.assume_init_ref().state, &state);
    state.fail_at   = 1;
    bool move_threw = false;
    try {
        auto value = rstd::move(slot).assume_init();
    } catch (int error) {
        move_threw = true;
        EXPECT_EQ(error, 42);
    }
    EXPECT_TRUE(move_threw);
    EXPECT_EQ(state.live, 0);
}

#endif

#if __has_feature(memory_sanitizer)
extern "C" rstd::intptr_t __msan_test_shadow(const volatile void*, rstd::size_t);

TEST(MaybeUninit, UninitFactoriesKeepStoragePoisoned) {
    using Slot = MaybeUninit<int>;
    Slot  plain;
    Slot  braces {};
    auto  factory = Slot::uninit();
    Slot* slots[] { &plain, &braces, &factory };
    for (auto slot : slots) {
        auto bytes = reinterpret_cast<const rstd::byte*>(slot);
        for (rstd::size_t i = 0; i < sizeof(Slot); ++i)
            EXPECT_EQ(__msan_test_shadow(bytes + i, 1), rstd::intptr_t(0));
        slot->write(42);
        EXPECT_EQ(__msan_test_shadow(slot, sizeof(Slot)), rstd::intptr_t(-1));
        EXPECT_EQ(slot->assume_init_read(), 42);
    }
    auto zero = Slot::zeroed();
    auto made = Slot::make(42);
    EXPECT_EQ(__msan_test_shadow(&zero, sizeof(zero)), rstd::intptr_t(-1));
    EXPECT_EQ(__msan_test_shadow(&made, sizeof(made)), rstd::intptr_t(-1));
}

TEST(MaybeUninit, HeapAndPlacementNewKeepStoragePoisoned) {
    using Slot = MaybeUninit<int>;
    auto check = [](const void* pointer, rstd::size_t size) {
        auto bytes = static_cast<const rstd::byte*>(pointer);
        for (rstd::size_t i = 0; i < size; ++i)
            EXPECT_EQ(__msan_test_shadow(bytes + i, 1), rstd::intptr_t(0));
    };
    Slot* slots[] { new Slot, new Slot(), new Slot {}, new Slot(Slot::uninit()) };
    for (auto slot : slots) {
        check(slot, sizeof(Slot));
        delete slot;
    }
    Slot* arrays[] { new Slot[3], new Slot[3](), new Slot[3] {} };
    for (auto slots : arrays) {
        check(slots, sizeof(Slot) * 3);
        delete[] slots;
    }
    auto storage = new rstd::byte[sizeof(Slot)];
    auto slot    = ::new (static_cast<void*>(storage)) Slot();
    check(slot, sizeof(Slot));
    rstd::destroy_at(slot);
    delete[] storage;
}
#endif
