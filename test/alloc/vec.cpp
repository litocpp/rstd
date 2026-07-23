#include <gtest/gtest.h>
import rstd;

static_assert(rstd::Impled<rstd::vec::Vec<int>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::vec::Vec<int>, rstd::ops::DerefMut>);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::vec::Vec<rstd::u8>&>().data()),
                                 rstd::byte*>);
static_assert(
    rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::vec::Vec<rstd::u8> const&>().data()),
                       rstd::byte const*>);
static_assert(! rstd::mtp::convertible_to<rstd::vec::SpareSlot<rstd::u8>, rstd::u8>);

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::string::String;
using rstd::vec::Vec;

namespace
{

template<typename T>
concept ConcreteCloneable = requires(const T& value) { value.clone(); };

struct MoveOnly {
    MoveOnly()                           = default;
    MoveOnly(const MoveOnly&)            = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&)                 = default;
    MoveOnly& operator=(MoveOnly&&)      = default;
};

struct SelfPointing {
    int        value;
    const int* value_ptr;

    explicit SelfPointing(int value): value(value), value_ptr(&this->value) {}
    SelfPointing(const SelfPointing&)            = delete;
    SelfPointing& operator=(const SelfPointing&) = delete;
    SelfPointing(SelfPointing&& other) noexcept: value(other.value), value_ptr(&value) {}
    SelfPointing& operator=(SelfPointing&& other) noexcept {
        value     = other.value;
        value_ptr = &value;
        return *this;
    }
};

struct CloneTracked : rstd::DefaultInClass<CloneTracked, rstd::clone::Clone> {
    int  value;
    int* clones;
    int* clone_froms;

    CloneTracked(int value, int& clones, int& clone_froms)
        : value(value), clones(&clones), clone_froms(&clone_froms) {}
    CloneTracked(const CloneTracked&)                    = delete;
    auto operator=(const CloneTracked&) -> CloneTracked& = delete;
    CloneTracked(CloneTracked&&)                         = default;
    auto operator=(CloneTracked&&) -> CloneTracked&      = default;

    auto clone() const -> CloneTracked {
        ++*clones;
        return CloneTracked { value, *clones, *clone_froms };
    }

    void clone_from(const CloneTracked& source) {
        value = source.value;
        ++*clone_froms;
    }
};

template<typename T>
concept ConstructibleFromSlice = requires(rstd::slice<T> values) { Vec<T>::from(values); };

static_assert(ConcreteCloneable<Vec<String>>);
static_assert(! ConcreteCloneable<Vec<MoveOnly>>);
static_assert(ConstructibleFromSlice<int>);
static_assert(ConstructibleFromSlice<CloneTracked>);
static_assert(! ConstructibleFromSlice<MoveOnly>);
static_assert(rstd::Impled<Vec<int>, rstd::convert::From<rstd::slice<int>>>);
static_assert(rstd::Impled<Vec<CloneTracked>, rstd::convert::From<rstd::slice<CloneTracked>>>);
static_assert(! rstd::Impled<Vec<MoveOnly>, rstd::convert::From<rstd::slice<MoveOnly>>>);

} // namespace

TEST(Vec, BasicPushPop) {
    Vec<int> v;
    EXPECT_EQ(v.len(), usize());
    EXPECT_TRUE(v.is_empty());

    v.push(1);
    v.push(2);
    v.push(3);

    EXPECT_EQ(v.len(), usize(3));
    EXPECT_FALSE(v.is_empty());

    EXPECT_EQ(v.pop(), Some(3));
    EXPECT_EQ(v.pop(), Some(2));
    EXPECT_EQ(v.pop(), Some(1));
    EXPECT_TRUE(v.pop().is_none());
    EXPECT_EQ(v.len(), usize());
}

TEST(Vec, Growth) {
    Vec<int> v = Vec<int>::with_capacity(usize(2));
    EXPECT_EQ(v.capacity(), usize(2));

    v.push(1);
    v.push(2);
    EXPECT_EQ(v.capacity(), usize(2));

    v.push(3);
    EXPECT_GT(v.capacity(), usize(2));
    EXPECT_EQ(v.len(), usize(3));

    EXPECT_EQ(v[usize()], 1);
    EXPECT_EQ(v[usize(1)], 2);
    EXPECT_EQ(v[usize(2)], 3);
}

TEST(Vec, GrowthMovesNonTrivialElements) {
    Vec<SelfPointing> values;
    for (int value = 0; value < 9; ++value) values.emplace_back(value);

    ASSERT_EQ(values.len(), usize(9));
    for (usize index {}; index < values.len(); ++index) {
        EXPECT_EQ(values[index].value, static_cast<int>(index.to_primitive()));
        EXPECT_EQ(values[index].value_ptr, &values[index].value);
    }
}

TEST(Vec, Indexing) {
    Vec<int> v;
    v.push(10);
    v.push(20);

    EXPECT_EQ(v[usize()], 10);
    EXPECT_EQ(v[usize(1)], 20);

    v[usize()] = 30;
    EXPECT_EQ(v[usize()], 30);
}

TEST(Vec, Destructor) {
    static int drop_count = 0;
    struct Dropper {
        Dropper()                          = default;
        Dropper(const Dropper&)            = delete;
        Dropper& operator=(const Dropper&) = delete;
        Dropper(Dropper&&) noexcept {}
        Dropper& operator=(Dropper&&) noexcept { return *this; }
        ~Dropper() { drop_count++; }
    };

    drop_count = 0;
    {
        Vec<Dropper> v;
        v.push(Dropper {});
        v.push(Dropper {});
    }
    // Each push involves a temporary and a move.
    // 2 (temporaries) + 2 (in vec) = 4 drops.
    EXPECT_EQ(drop_count, 4);
}

TEST(Vec, IntoBoxedSlice) {
    Vec<int> v;
    v.push(1);
    v.push(2);

    auto b = v.into_boxed_slice();
    EXPECT_EQ(v.len(), usize());
    // Box<T[]> should have metadata for length
    EXPECT_EQ(b.as_ptr().len(), usize(2));
    EXPECT_EQ(b.as_ptr()[usize()], 1);
    EXPECT_EQ(b.as_ptr()[usize(1)], 2);
    EXPECT_EQ(rstd::alloc::Layout::for_value(b.as_ptr()).size, usize(2 * sizeof(int)));
}

TEST(Vec, BoxedSliceRoundTripTransfersExactAllocation) {
    auto values = Vec<rstd::u8>::with_capacity(usize(3));
    values.push(u8(3));
    values.push(u8(5));
    values.push(u8(8));
    auto* allocation = values.data();

    auto boxed = values.into_boxed_slice();
    EXPECT_EQ(boxed.get(), allocation);

    auto round_trip = Vec<rstd::u8>::from_boxed_slice(rstd::move(boxed));
    EXPECT_EQ(round_trip.data(), allocation);
    EXPECT_EQ(round_trip.capacity(), usize(3));
    EXPECT_EQ(round_trip.as_slice(), "\x03\x05\x08"_bytes);
}

TEST(Vec, EmptyBoxedSliceRoundTripStaysEmpty) {
    auto boxed = Vec<int>::make().into_boxed_slice();
    EXPECT_EQ(boxed.as_ptr().len(), usize());

    auto values = Vec<int>::from_boxed_slice(rstd::move(boxed));
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.capacity(), usize());
}

TEST(Vec, BoxedSliceDropsEveryElement) {
    struct DropProbe {
        int* drops;

        explicit DropProbe(int& drops): drops(&drops) {}
        DropProbe(const DropProbe&)            = delete;
        DropProbe& operator=(const DropProbe&) = delete;

        DropProbe(DropProbe&& other) noexcept: drops(other.drops) { other.drops = nullptr; }

        ~DropProbe() {
            if (drops != nullptr) ++*drops;
        }
    };

    int  drops  = 0;
    auto values = Vec<DropProbe>::make();
    values.push(DropProbe { drops });
    values.push(DropProbe { drops });

    {
        auto boxed = values.into_boxed_slice();
        EXPECT_EQ(drops, 0);
    }
    EXPECT_EQ(drops, 2);
}

TEST(Vec, Remove) {
    Vec<int> v;
    v.push(1);
    v.push(2);
    v.push(3);

    EXPECT_EQ(v.remove(usize(1)), 2);
    EXPECT_EQ(v.len(), usize(2));
    EXPECT_EQ(v[usize()], 1);
    EXPECT_EQ(v[usize(1)], 3);
}

TEST(Vec, RetainPreservesOrderAndDropsRemovedElements) {
    struct Tracked {
        int* drops;
        int  value;

        Tracked(int& count, int number): drops(&count), value(number) {}
        Tracked(const Tracked&)            = delete;
        Tracked& operator=(const Tracked&) = delete;
        Tracked(Tracked&& other) noexcept: drops(other.drops), value(other.value) {
            other.drops = nullptr;
        }
        Tracked& operator=(Tracked&&) = delete;
        ~Tracked() {
            if (drops != nullptr) ++*drops;
        }
    };

    int  drops  = 0;
    auto values = Vec<Tracked>::make();
    for (int value = 0; value < 6; ++value) values.emplace_back(drops, value);

    values.retain([](const Tracked& value) {
        return value.value % 2 == 0;
    });

    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()].value, 0);
    EXPECT_EQ(values[usize(1)].value, 2);
    EXPECT_EQ(values[usize(2)].value, 4);
    EXPECT_EQ(drops, 3);

    values.retain([](const Tracked&) {
        return false;
    });
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(drops, 6);
}

TEST(Vec, ReserveAndExtendFromSlice) {
    Vec<rstd::u8> v;
    v.reserve(usize(8));
    EXPECT_GE(v.capacity(), usize(8));
    EXPECT_EQ(v.len(), usize());

    auto data = rstd::array<rstd::u8, 3> { u8(1), u8(2), u8(3) };
    v.extend_from_slice(data.as_slice());

    EXPECT_EQ(v.len(), usize(3));
    EXPECT_EQ(v[usize()], u8(1));
    EXPECT_EQ(v[usize(1)], u8(2));
    EXPECT_EQ(v[usize(2)], u8(3));
}

TEST(Vec, U8GrowthKeepsByteStorageAndProxyWrites) {
    auto values = Vec<rstd::u8>::with_capacity(usize(1));
    for (rstd::uint16_t value = 0; value < 256; ++value) {
        values.push(rstd::u8(static_cast<rstd::uint8_t>(value)));
    }

    ASSERT_EQ(values.len(), usize(256));
    static_assert(rstd::mtp::same_as<decltype(values[usize()]), rstd::mut_ref<rstd::u8>>);
    values[usize(128)] = rstd::u8(17);
    EXPECT_EQ(values[usize(128)], rstd::u8(17));
    EXPECT_EQ(values.data()[128], rstd::byte { 17 });

    auto boxed = values.into_boxed_slice();
    static_assert(rstd::mtp::same_as<decltype(boxed.get()), rstd::byte*>);
    EXPECT_EQ(boxed.as_ptr()[usize(128)], rstd::u8(17));
}

TEST(Vec, FromSliceOwnsIndependentCopy) {
    int  source[] { 3, 5, 8 };
    auto source_slice = rstd::slice<int>::from_raw_parts(source, usize(3));
    auto values = rstd::Impl<rstd::convert::From<rstd::slice<int>>, Vec<int>>::from(source_slice);

    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values.capacity(), usize(3));
    EXPECT_EQ(values[usize()], 3);
    EXPECT_EQ(values[usize(1)], 5);
    EXPECT_EQ(values[usize(2)], 8);

    source[1]        = 13;
    values[usize(2)] = 21;
    EXPECT_EQ(values[usize(1)], 5);
    EXPECT_EQ(source[2], 8);
}

TEST(Vec, FromEmptySliceDoesNotAllocate) {
    auto values = Vec<int>::from(rstd::slice<int>());
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.capacity(), usize());
}

TEST(Vec, FromSliceSupportsCloneOnlyElements) {
    int          clones      = 0;
    int          clone_froms = 0;
    CloneTracked source[] { CloneTracked { 2, clones, clone_froms },
                            CloneTracked { 7, clones, clone_froms } };

    auto values =
        Vec<CloneTracked>::from(rstd::slice<CloneTracked>::from_raw_parts(source, usize(2)));

    ASSERT_EQ(values.len(), usize(2));
    EXPECT_EQ(values[usize()].value, 2);
    EXPECT_EQ(values[usize(1)].value, 7);
    EXPECT_EQ(clones, 2);
    EXPECT_EQ(clone_froms, 0);
}

TEST(Vec, ExtendFromOwnSliceSurvivesGrowth) {
    auto values = Vec<int>::with_capacity(usize(2));
    values.push(1);
    values.push(2);

    values.extend_from_slice(values.as_slice());

    ASSERT_EQ(values.len(), usize(4));
    EXPECT_EQ(values[usize()], 1);
    EXPECT_EQ(values[usize(1)], 2);
    EXPECT_EQ(values[usize(2)], 1);
    EXPECT_EQ(values[usize(3)], 2);
}

TEST(Vec, ExtendFromPointerClonesWithoutGrowth) {
    int          clones      = 0;
    int          clone_froms = 0;
    CloneTracked source[] { CloneTracked { 3, clones, clone_froms },
                            CloneTracked { 5, clones, clone_froms } };
    auto         values     = Vec<CloneTracked>::with_capacity(usize(3));
    auto*        allocation = values.data();

    values.extend_from_slice(source, usize(2));

    EXPECT_EQ(values.data(), allocation);
    ASSERT_EQ(values.len(), usize(2));
    EXPECT_EQ(values[usize()].value, 3);
    EXPECT_EQ(values[usize(1)].value, 5);
    EXPECT_EQ(clones, 2);
    EXPECT_EQ(clone_froms, 0);
}

TEST(Vec, CloneFromReusesCapacityAndInitializedElements) {
    int clones      = 0;
    int clone_froms = 0;

    auto source = Vec<CloneTracked>::make();
    source.push(CloneTracked { 4, clones, clone_froms });
    source.push(CloneTracked { 9, clones, clone_froms });
    source.push(CloneTracked { 16, clones, clone_froms });

    auto target = Vec<CloneTracked>::with_capacity(usize(4));
    target.push(CloneTracked { 1, clones, clone_froms });
    target.push(CloneTracked { 2, clones, clone_froms });
    auto* allocation = target.data();

    const auto& const_source = source;
    target.clone_from(const_source);

    EXPECT_EQ(target.data(), allocation);
    ASSERT_EQ(target.len(), usize(3));
    EXPECT_EQ(target[usize()].value, 4);
    EXPECT_EQ(target[usize(1)].value, 9);
    EXPECT_EQ(target[usize(2)].value, 16);
    EXPECT_EQ(clone_froms, 2);
    EXPECT_EQ(clones, 1);
}

TEST(Vec, CloneFromTruncatesLongerTarget) {
    int clones      = 0;
    int clone_froms = 0;

    auto source = Vec<CloneTracked>::make();
    source.push(CloneTracked { 6, clones, clone_froms });

    auto target = Vec<CloneTracked>::with_capacity(usize(3));
    target.push(CloneTracked { 1, clones, clone_froms });
    target.push(CloneTracked { 2, clones, clone_froms });
    target.push(CloneTracked { 3, clones, clone_froms });

    target.clone_from(source);

    ASSERT_EQ(target.len(), usize(1));
    EXPECT_EQ(target[usize()].value, 6);
    EXPECT_EQ(clone_froms, 1);
    EXPECT_EQ(clones, 0);
}

TEST(Vec, CloneFromEqualLengthReusesEveryElement) {
    int clones      = 0;
    int clone_froms = 0;

    auto source = Vec<CloneTracked>::make();
    source.push(CloneTracked { 8, clones, clone_froms });
    source.push(CloneTracked { 13, clones, clone_froms });

    auto target = Vec<CloneTracked>::with_capacity(usize(2));
    target.push(CloneTracked { 1, clones, clone_froms });
    target.push(CloneTracked { 2, clones, clone_froms });
    auto* allocation = target.data();

    target.clone_from(source);

    EXPECT_EQ(target.data(), allocation);
    ASSERT_EQ(target.len(), usize(2));
    EXPECT_EQ(target[usize()].value, 8);
    EXPECT_EQ(target[usize(1)].value, 13);
    EXPECT_EQ(clone_froms, 2);
    EXPECT_EQ(clones, 0);
}

TEST(Vec, SpareCapacityAndSetLen) {
    auto v = Vec<rstd::u8>::with_capacity(usize(4));

    auto spare = v.spare_capacity_mut();
    ASSERT_EQ(spare.len(), usize(4));
    spare[usize()].write(u8(7));
    spare[usize(1)].write(u8(8));
    spare[usize(2)].write(u8(9));
    v.set_len_unchecked(usize(3));

    EXPECT_EQ(v.len(), usize(3));
    EXPECT_EQ(v[usize()], u8(7));
    EXPECT_EQ(v[usize(1)], u8(8));
    EXPECT_EQ(v[usize(2)], u8(9));

    v.truncate(usize(2));
    EXPECT_EQ(v.len(), usize(2));
    EXPECT_EQ(v[usize(1)], u8(8));

    EXPECT_DEATH(v.spare_capacity_mut()[usize(2)].write(u8(1)),
                 "Vec spare capacity index out of bounds");
}

TEST(Vec, SpareCapacityConstructsNonTrivialElements) {
    struct Tracked {
        int* drops;
        int  value;

        Tracked(int& count, int number): drops(&count), value(number) {}
        Tracked(const Tracked&)            = delete;
        Tracked& operator=(const Tracked&) = delete;
        Tracked(Tracked&& other) noexcept: drops(other.drops), value(other.value) {
            other.drops = nullptr;
        }
        ~Tracked() {
            if (drops != nullptr) ++*drops;
        }
    };

    int drops = 0;
    {
        auto values = Vec<Tracked>::with_capacity(usize(1));
        values.spare_capacity_mut()[usize()].write(Tracked(drops, 7));
        values.set_len_unchecked(usize(1));
        EXPECT_EQ(values[usize()].value, 7);
        EXPECT_EQ(drops, 0);
    }
    EXPECT_EQ(drops, 1);
}

TEST(Vec, Resize) {
    Vec<int> v;
    v.resize(usize(3), 5);

    ASSERT_EQ(v.len(), usize(3));
    EXPECT_EQ(v[usize()], 5);
    EXPECT_EQ(v[usize(1)], 5);
    EXPECT_EQ(v[usize(2)], 5);

    v.resize(usize(1), 0);
    EXPECT_EQ(v.len(), usize(1));
    EXPECT_EQ(v[usize()], 5);
}

TEST(Vec, ResizeFromOwnElementSurvivesGrowth) {
    auto values = Vec<int>::with_capacity(usize(1));
    values.push(17);

    values.resize(usize(3), values[usize()]);

    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()], 17);
    EXPECT_EQ(values[usize(1)], 17);
    EXPECT_EQ(values[usize(2)], 17);
}

TEST(Vec, CloneOwnsIndependentElements) {
    auto values = Vec<String>::make();
    values.push(String::make("alpha"_str));
    values.push(String::make("beta"_str));

    auto direct   = values.clone();
    auto abstract = rstd::as<rstd::clone::Clone>(values).clone();
    values[usize()].push_ascii(u8('!'));

    ASSERT_EQ(direct.len(), usize(2));
    ASSERT_EQ(abstract.len(), usize(2));
    EXPECT_EQ(values[usize()], "alpha!"_str);
    EXPECT_EQ(direct[usize()], "alpha"_str);
    EXPECT_EQ(direct[usize(1)], "beta"_str);
    EXPECT_EQ(abstract[usize()], "alpha"_str);
    EXPECT_EQ(abstract[usize(1)], "beta"_str);
}
