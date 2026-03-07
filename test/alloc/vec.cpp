#include <gtest/gtest.h>
import rstd;

using namespace rstd;

TEST(Vec, BasicPushPop) {
    rstd::Vec<int> v;
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
    rstd::Vec<int> v = rstd::Vec<int>::with_capacity(2);
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
    rstd::Vec<int> v;
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
        Dropper() = default;
        Dropper(const Dropper&) = delete;
        Dropper& operator=(const Dropper&) = delete;
        Dropper(Dropper&&) noexcept {}
        Dropper& operator=(Dropper&&) noexcept { return *this; }
        ~Dropper() { drop_count++; }
    };

    drop_count = 0;
    {
        rstd::Vec<Dropper> v;
        v.push(Dropper{});
        v.push(Dropper{});
    }
    // Each push involves a temporary and a move.
    // 2 (temporaries) + 2 (in vec) = 4 drops.
    EXPECT_EQ(drop_count, 4);
}

TEST(Vec, IntoBoxedSlice) {
    rstd::Vec<int> v;
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
    rstd::Vec<int> v;
    v.push(1);
    v.push(2);
    v.push(3);

    EXPECT_EQ(v.remove(1), 2);
    EXPECT_EQ(v.len(), 2);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 3);
}
