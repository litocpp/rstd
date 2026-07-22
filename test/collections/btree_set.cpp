#include <gtest/gtest.h>
import rstd;

using namespace rstd::prelude;
using rstd::collections::BTreeSet;

TEST(BTreeSet, IteratesInOrderAndSupportsBorrowedStringLookup) {
    auto set = BTreeSet<rstd::string::String>::make();
    set.insert(rstd::string::String::make("gamma"));
    set.insert(rstd::string::String::make("alpha"));
    set.insert(rstd::string::String::make("beta"));

    auto iter = set.iter();
    EXPECT_EQ(**iter.next(), rstd::ref<rstd::str>("alpha"));
    EXPECT_EQ(**iter.next(), rstd::ref<rstd::str>("beta"));
    EXPECT_EQ(**iter.next(), rstd::ref<rstd::str>("gamma"));
    EXPECT_TRUE(iter.next().is_none());
    EXPECT_TRUE(set.contains(rstd::ref<rstd::str>("beta")));
    EXPECT_EQ(**set.get(rstd::ref<rstd::str>("gamma")), rstd::ref<rstd::str>("gamma"));
}

TEST(BTreeSet, RetainCloneAndRemovalPreserveOwnership) {
    auto set = BTreeSet<i32>::make();
    for (i32 value {}; value < i32(100); value += i32(1)) set.insert(value);

    set.retain([](const i32& value) {
        return value % i32(2) == i32();
    });
    EXPECT_EQ(set.len(), usize(50));
    EXPECT_EQ(**set.first(), i32());
    EXPECT_EQ(**set.last(), i32(98));

    auto cloned = set.clone();
    EXPECT_EQ(set.take(i32(10)), Some(i32(10)));
    EXPECT_TRUE(cloned.contains(i32(10)));
    EXPECT_EQ(cloned.pop_first(), Some(i32()));
    EXPECT_EQ(cloned.pop_last(), Some(i32(98)));
}
