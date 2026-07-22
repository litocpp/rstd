#include <gtest/gtest.h>
import rstd;

static_assert(rstd::Impled<rstd::vec::Vec<int>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::vec::Vec<int>, rstd::ops::DerefMut>);

using namespace rstd::prelude;
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

static_assert(ConcreteCloneable<Vec<String>>);
static_assert(! ConcreteCloneable<Vec<MoveOnly>>);

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

    rstd::u8 data[] { u8(1), u8(2), u8(3) };
    v.extend_from_slice(rstd::slice<rstd::u8>::from_raw_parts(data, usize(3)));

    EXPECT_EQ(v.len(), usize(3));
    EXPECT_EQ(v[usize()], u8(1));
    EXPECT_EQ(v[usize(1)], u8(2));
    EXPECT_EQ(v[usize(2)], u8(3));
}

TEST(Vec, SpareCapacityAndSetLen) {
    auto v = Vec<rstd::u8>::with_capacity(usize(4));

    auto spare = v.spare_capacity_mut();
    ASSERT_EQ(spare.len(), usize(4));
    spare[usize()]  = u8(7);
    spare[usize(1)] = u8(8);
    spare[usize(2)] = u8(9);
    v.set_len_unchecked(usize(3));

    EXPECT_EQ(v.len(), usize(3));
    EXPECT_EQ(v[usize()], u8(7));
    EXPECT_EQ(v[usize(1)], u8(8));
    EXPECT_EQ(v[usize(2)], u8(9));

    v.truncate(usize(2));
    EXPECT_EQ(v.len(), usize(2));
    EXPECT_EQ(v[usize(1)], u8(8));
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

TEST(Vec, CloneOwnsIndependentElements) {
    auto values = Vec<String>::make();
    values.push(String::make("alpha"));
    values.push(String::make("beta"));

    auto direct   = values.clone();
    auto abstract = rstd::as<rstd::clone::Clone>(values).clone();
    values[usize()].push_back('!');

    ASSERT_EQ(direct.len(), usize(2));
    ASSERT_EQ(abstract.len(), usize(2));
    EXPECT_EQ(values[usize()], "alpha!");
    EXPECT_EQ(direct[usize()], "alpha");
    EXPECT_EQ(direct[usize(1)], "beta");
    EXPECT_EQ(abstract[usize()], "alpha");
    EXPECT_EQ(abstract[usize(1)], "beta");
}
