#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::collections::HashSet;

TEST(HashSet, ExtendIgnoresDuplicates) {
    auto set = HashSet<i32>::make();
    set.insert(2_i32);
    rstd::iter::extend(set, rstd::iter::range(0_i32, 4_i32));

    EXPECT_EQ(set.len(), 4_usize);
    EXPECT_TRUE(set.contains(0_i32));
    EXPECT_TRUE(set.contains(3_i32));
}

TEST(HashSet, InsertBorrowRemoveAndRetain) {
    auto set = HashSet<rstd::string::String>::with_capacity(usize(2));

    EXPECT_TRUE(set.insert(rstd::string::String::make("alpha"_str)));
    EXPECT_TRUE(set.insert(rstd::string::String::make("beta"_str)));
    EXPECT_FALSE(set.insert(rstd::string::String::make("alpha"_str)));
    EXPECT_TRUE(set.contains(rstd::ref<rstd::str>("beta"_str)));
    EXPECT_EQ(**set.get(rstd::ref<rstd::str>("alpha"_str)), rstd::ref<rstd::str>("alpha"_str));

    set.retain([](const rstd::string::String& value) {
        return value == "alpha"_str;
    });
    EXPECT_EQ(set.len(), usize(1));
    EXPECT_TRUE(set.remove(rstd::ref<rstd::str>("alpha"_str)));
    EXPECT_TRUE(set.is_empty());
}

TEST(HashSet, CloneAndOwningIterationAreIndependent) {
    auto source = HashSet<rstd::string::String>::make();
    source.insert(rstd::string::String::make("alpha"_str));
    source.insert(rstd::string::String::make("beta"_str));

    auto cloned = source.clone();
    EXPECT_TRUE(source.remove(rstd::ref<rstd::str>("alpha"_str)));
    EXPECT_TRUE(cloned.contains(rstd::ref<rstd::str>("alpha"_str)));

    auto  values = rstd::move(cloned).into_iter();
    usize count {};
    for (auto value = values.next(); value.is_some(); value = values.next()) ++count;
    EXPECT_EQ(count, usize(2));
    EXPECT_TRUE(cloned.is_empty());
}

TEST(HashSet, IntoIteratorSupportsOwnedAndBorrowedRangeFor) {
    auto set = HashSet<i32>::make();
    set.insert(1_i32);
    set.insert(2_i32);

    auto borrowed =
        rstd::iter::into_iter(rstd::ref<HashSet<i32>>::from_raw_parts(rstd::addressof(set)));
    auto borrowed_total = i32();
    for (auto value : borrowed) borrowed_total += *value;
    EXPECT_EQ(borrowed_total, 3_i32);

    auto mutable_borrowed =
        rstd::iter::into_iter(rstd::mut_ref<HashSet<i32>>::from_raw_parts(rstd::addressof(set)));
    static_assert(rstd::mtp::same_as<typename decltype(mutable_borrowed)::Item, rstd::ref<i32>>);
    EXPECT_EQ(rstd::move(mutable_borrowed).count(), 2_usize);

    auto owned_total = i32();
    for (auto value : rstd::iter::into_iter(rstd::move(set))) owned_total += value;
    EXPECT_EQ(owned_total, 3_i32);
}
