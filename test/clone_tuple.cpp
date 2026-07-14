#include <gtest/gtest.h>

import rstd;

using namespace rstd::prelude;

namespace
{

struct CloneOnly : DefaultInClass<CloneOnly, Clone> {
    int  value;
    int* clones;

    CloneOnly(int value, int& clones): value(value), clones(&clones) {}

    CloneOnly(const CloneOnly&)                    = delete;
    auto operator=(const CloneOnly&) -> CloneOnly& = delete;
    CloneOnly(CloneOnly&&)                         = default;
    auto operator=(CloneOnly&&) -> CloneOnly&      = default;

    auto clone() const -> CloneOnly {
        ++*clones;
        return CloneOnly { value, *clones };
    }
};

} // namespace

TEST(CloneTuple, CopyConstructionAndAssignmentCloneEveryElement) {
    int  clones = 0;
    auto source = rstd::make_clone_tuple(CloneOnly { 1, clones }, CloneOnly { 2, clones });

    static_assert(rstd::mtp::copy<decltype(source)>);
    static_assert(rstd::mtp::assign_copy<decltype(source)>);

    auto copied = source;
    EXPECT_EQ(clones, 2);
    EXPECT_EQ(copied.get<0>().value, 1);
    EXPECT_EQ(copied.get<1>().value, 2);

    auto assigned = rstd::make_clone_tuple(CloneOnly { 0, clones }, CloneOnly { 0, clones });
    assigned      = source;
    EXPECT_EQ(clones, 4);
    EXPECT_EQ(assigned.get<0>().value, 1);
    EXPECT_EQ(assigned.get<1>().value, 2);

    auto cloned = rstd::as<Clone>(source).clone();
    EXPECT_EQ(clones, 6);
    EXPECT_EQ(cloned.get<0>().value, 1);
    EXPECT_EQ(cloned.get<1>().value, 2);
}

TEST(CloneTuple, SupportsTupleAccessAndApply) {
    int  clones           = 0;
    auto values           = rstd::make_clone_tuple(CloneOnly { 5, clones }, 7);
    auto& [owned, number] = values;

    EXPECT_EQ(owned.value, 5);
    EXPECT_EQ(number, 7);
    EXPECT_EQ(rstd::get<1>(values), 7);
    EXPECT_EQ(rstd::apply(
                  [](const CloneOnly& value, int number) {
                      return value.value + number;
                  },
                  values),
              12);
}

TEST(CloneTuple, MakesCloneOnlyLambdaCaptureCopyConstructible) {
    auto state    = rstd::sync::Arc<int>::make(42);
    auto callback = [captures = rstd::make_clone_tuple(state.clone())] {
        return *captures.get<0>();
    };

    static_assert(rstd::mtp::copy<decltype(callback)>);
    EXPECT_EQ(state.strong_count(), 2u);

    auto copied = callback;
    EXPECT_EQ(state.strong_count(), 3u);
    EXPECT_EQ(callback(), 42);
    EXPECT_EQ(copied(), 42);
}
