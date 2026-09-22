#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

constexpr bool swap_byte_storage() {
    rstd::byte data[]  = { rstd::byte { 3 }, rstd::byte { 1 } };
    auto       pointer = rstd::mut_ptr<u8>::from_raw_parts(data);
    rstd::ptr_::swap(pointer, pointer.add(1_usize));
    rstd::ptr_::swap(pointer, pointer);
    return data[0] == rstd::byte { 1 } && data[1] == rstd::byte { 3 };
}
static_assert(swap_byte_storage());

TEST(Ptr, SwapLogicalBytes) {
    EXPECT_TRUE(swap_byte_storage());
    rstd::ptr_::swap(rstd::mut_ptr<u8> {}, rstd::mut_ptr<u8> {});
}

TEST(Ptr, CopyNonoverlappingUsesElementCount) {
    struct Pair {
        int first;
        int second;
    };

    Pair source[] { { 1, 2 }, { 3, 4 } };
    Pair destination[] { {}, {} };
    rstd::ptr_::copy_nonoverlapping(rstd::ptr<Pair>::from_raw_parts(source),
                                    rstd::mut_ptr<Pair>::from_raw_parts(destination),
                                    usize(2));

    EXPECT_EQ(destination[0].first, 1);
    EXPECT_EQ(destination[0].second, 2);
    EXPECT_EQ(destination[1].first, 3);
    EXPECT_EQ(destination[1].second, 4);
}

TEST(Ptr, CopyNonoverlappingAcceptsEmptyNullRange) {
    rstd::ptr_::copy_nonoverlapping(rstd::ptr<int>::from_raw_parts(nullptr),
                                    rstd::mut_ptr<int>::from_raw_parts(nullptr),
                                    usize());
}
