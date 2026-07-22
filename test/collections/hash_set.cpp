#include <gtest/gtest.h>
import rstd;

using namespace rstd::prelude;
using rstd::collections::HashSet;

TEST(HashSet, InsertBorrowRemoveAndRetain) {
    auto set = HashSet<rstd::string::String>::with_capacity(usize(2));

    EXPECT_TRUE(set.insert(rstd::string::String::make("alpha")));
    EXPECT_TRUE(set.insert(rstd::string::String::make("beta")));
    EXPECT_FALSE(set.insert(rstd::string::String::make("alpha")));
    EXPECT_TRUE(set.contains(rstd::ref<rstd::str>("beta")));
    EXPECT_EQ(**set.get(rstd::ref<rstd::str>("alpha")), rstd::ref<rstd::str>("alpha"));

    set.retain([](const rstd::string::String& value) {
        return value == "alpha";
    });
    EXPECT_EQ(set.len(), usize(1));
    EXPECT_TRUE(set.remove(rstd::ref<rstd::str>("alpha")));
    EXPECT_TRUE(set.is_empty());
}

TEST(HashSet, CloneAndOwningIterationAreIndependent) {
    auto source = HashSet<rstd::string::String>::make();
    source.insert(rstd::string::String::make("alpha"));
    source.insert(rstd::string::String::make("beta"));

    auto cloned = source.clone();
    EXPECT_TRUE(source.remove(rstd::ref<rstd::str>("alpha")));
    EXPECT_TRUE(cloned.contains(rstd::ref<rstd::str>("alpha")));

    auto  values = cloned.into_iter();
    usize count {};
    for (auto value = values.next(); value.is_some(); value = values.next()) ++count;
    EXPECT_EQ(count, usize(2));
    EXPECT_TRUE(cloned.is_empty());
}
