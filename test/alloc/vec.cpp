#include <gtest/gtest.h>
import rstd;

using namespace rstd::prelude;
using rstd::string::String;
using rstd::vec::Vec;

namespace
{

template<typename T>
concept ConcreteCloneable = requires(const T& value) { value.clone(); };

struct MoveOnly {
    MoveOnly()                            = default;
    MoveOnly(const MoveOnly&)             = delete;
    MoveOnly& operator=(const MoveOnly&)  = delete;
    MoveOnly(MoveOnly&&)                  = default;
    MoveOnly& operator=(MoveOnly&&)       = default;
};

static_assert(ConcreteCloneable<Vec<String>>);
static_assert(! ConcreteCloneable<Vec<MoveOnly>>);

} // namespace

TEST(Vec, BasicPushPop) {
    Vec<int> v;
    EXPECT_EQ(v.len(), 0);
    EXPECT_TRUE(v.is_empty());

    v.push(1);
    v.push(2);
    v.push(3);

    EXPECT_EQ(v.len(), 3);
    EXPECT_FALSE(v.is_empty());

    EXPECT_EQ(v.pop(), Some(3));
    EXPECT_EQ(v.pop(), Some(2));
    EXPECT_EQ(v.pop(), Some(1));
    EXPECT_TRUE(v.pop().is_none());
    EXPECT_EQ(v.len(), 0);
}

TEST(Vec, Growth) {
    Vec<int> v = Vec<int>::with_capacity(2);
    EXPECT_EQ(v.capacity(), 2);

    v.push(1);
    v.push(2);
    EXPECT_EQ(v.capacity(), 2);

    v.push(3);
    EXPECT_GT(v.capacity(), 2);
    EXPECT_EQ(v.len(), 3);

    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
}

TEST(Vec, Indexing) {
    Vec<int> v;
    v.push(10);
    v.push(20);

    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[1], 20);

    v[0] = 30;
    EXPECT_EQ(v[0], 30);
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
    EXPECT_EQ(v.len(), 0);
    // Box<T[]> should have metadata for length
    EXPECT_EQ(b.as_ptr().len(), 2);
    EXPECT_EQ(b.as_ptr()[0], 1);
    EXPECT_EQ(b.as_ptr()[1], 2);
}

TEST(Vec, Remove) {
    Vec<int> v;
    v.push(1);
    v.push(2);
    v.push(3);

    EXPECT_EQ(v.remove(1), 2);
    EXPECT_EQ(v.len(), 2);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 3);
}

TEST(Vec, ReserveAndExtendFromSlice) {
    Vec<rstd::u8> v;
    v.reserve(8);
    EXPECT_GE(v.capacity(), 8);
    EXPECT_EQ(v.len(), 0);

    rstd::u8 data[] { 1, 2, 3 };
    v.extend_from_slice(rstd::slice<rstd::u8>::from_raw_parts(data, 3));

    EXPECT_EQ(v.len(), 3);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
}

TEST(Vec, SpareCapacityAndSetLen) {
    auto v = Vec<rstd::u8>::with_capacity(4);

    auto spare = v.spare_capacity_mut();
    ASSERT_EQ(spare.len(), 4);
    spare[0] = 7;
    spare[1] = 8;
    spare[2] = 9;
    v.set_len_unchecked(3);

    EXPECT_EQ(v.len(), 3);
    EXPECT_EQ(v[0], 7);
    EXPECT_EQ(v[1], 8);
    EXPECT_EQ(v[2], 9);

    v.truncate(2);
    EXPECT_EQ(v.len(), 2);
    EXPECT_EQ(v[1], 8);
}

TEST(Vec, Resize) {
    Vec<int> v;
    v.resize(3, 5);

    ASSERT_EQ(v.len(), 3);
    EXPECT_EQ(v[0], 5);
    EXPECT_EQ(v[1], 5);
    EXPECT_EQ(v[2], 5);

    v.resize(1, 0);
    EXPECT_EQ(v.len(), 1);
    EXPECT_EQ(v[0], 5);
}

TEST(Vec, CloneOwnsIndependentElements) {
    auto values = Vec<String>::make();
    values.push(String::make("alpha"));
    values.push(String::make("beta"));

    auto direct   = values.clone();
    auto abstract = rstd::as<rstd::clone::Clone>(values).clone();
    values[0].push_back('!');

    ASSERT_EQ(direct.len(), 2u);
    ASSERT_EQ(abstract.len(), 2u);
    EXPECT_EQ(values[0], "alpha!");
    EXPECT_EQ(direct[0], "alpha");
    EXPECT_EQ(direct[1], "beta");
    EXPECT_EQ(abstract[0], "alpha");
    EXPECT_EQ(abstract[1], "beta");
}
