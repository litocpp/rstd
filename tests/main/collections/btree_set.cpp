#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::collections::BTreeSet;

TEST(BTreeSet, ExtendIgnoresDuplicatesAndKeepsOrder) {
    auto set = BTreeSet<i32>::make();
    set.insert(2_i32);
    rstd::iter::extend(set, rstd::iter::range(0_i32, 4_i32));

    EXPECT_EQ(set.len(), 4_usize);
    EXPECT_EQ(**set.first(), 0_i32);
    EXPECT_EQ(**set.last(), 3_i32);
}

TEST(BTreeSet, IteratesInOrderAndSupportsBorrowedStringLookup) {
    auto set = BTreeSet<rstd::string::String>::make();
    set.insert(rstd::string::String::make("gamma"_str));
    set.insert(rstd::string::String::make("alpha"_str));
    set.insert(rstd::string::String::make("beta"_str));

    auto iter = set.iter();
    EXPECT_EQ(**iter.next(), "alpha"_str);
    EXPECT_EQ(**iter.next(), "beta"_str);
    EXPECT_EQ(**iter.next(), "gamma"_str);
    EXPECT_TRUE(iter.next().is_none());
    EXPECT_TRUE(set.contains("beta"_str));
    EXPECT_EQ(**set.get("gamma"_str), "gamma"_str);
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

TEST(BTreeSet, IntoIteratorSupportsOwnedAndBorrowedRangeFor) {
    auto set = BTreeSet<i32>::make();
    set.insert(1_i32);
    set.insert(2_i32);

    auto borrowed =
        rstd::iter::into_iter(rstd::ref<BTreeSet<i32>>::from_raw_parts(rstd::addressof(set)));
    auto borrowed_total = i32();
    for (auto value : borrowed) borrowed_total += *value;
    EXPECT_EQ(borrowed_total, 3_i32);

    auto mutable_borrowed =
        rstd::iter::into_iter(rstd::mut_ref<BTreeSet<i32>>::from_raw_parts(rstd::addressof(set)));
    static_assert(rstd::mtp::same_as<typename decltype(mutable_borrowed)::Item, rstd::ref<i32>>);
    EXPECT_EQ(rstd::move(mutable_borrowed).count(), 2_usize);

    auto owned_total = i32();
    for (auto value : rstd::iter::into_iter(rstd::move(set))) owned_total += value;
    EXPECT_EQ(owned_total, 3_i32);
}
