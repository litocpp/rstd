#include <gtest/gtest.h>
import rstd;

using namespace rstd;

namespace
{

struct MoveOnly {
    int value;

    explicit MoveOnly(int value): value(value) {}

    MoveOnly(const MoveOnly&)                        = delete;
    MoveOnly& operator=(const MoveOnly&)             = delete;
    MoveOnly(MoveOnly&&) noexcept                    = default;
    auto operator=(MoveOnly&&) noexcept -> MoveOnly& = default;
};

} // namespace

TEST(Hint, BlackBoxPreservesLvalue) {
    int value = 7;

    auto& out = hint::black_box(value);

    EXPECT_EQ(out, 7);
    out = 9;
    EXPECT_EQ(value, 9);
}

TEST(Hint, BlackBoxSupportsRvalueAndMoveOnly) {
    auto moved = hint::black_box(MoveOnly { 11 });

    EXPECT_EQ(moved.value, 11);
}
