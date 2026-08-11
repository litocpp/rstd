#include <rstd/test/gtest.hpp>
import rstd.core;
using rstd::num::nonzero::NonZero;
using namespace rstd;

template<typename T>
inline constexpr bool has_nonzero_niche =
    sizeof(NonZero<T>) == sizeof(T) && sizeof(Option<NonZero<T>>) == sizeof(T);

static_assert(has_nonzero_niche<u8>);
static_assert(has_nonzero_niche<u16>);
static_assert(has_nonzero_niche<u32>);
static_assert(has_nonzero_niche<u64>);
static_assert(has_nonzero_niche<u128>);
static_assert(has_nonzero_niche<usize>);
static_assert(has_nonzero_niche<i8>);
static_assert(has_nonzero_niche<i16>);
static_assert(has_nonzero_niche<i32>);
static_assert(has_nonzero_niche<i64>);
static_assert(has_nonzero_niche<i128>);
static_assert(has_nonzero_niche<isize>);

TEST(NonZero, Basic) {
    auto non = NonZero<u32>::make(u32());
    auto ok  = NonZero<u32>::make(u32(1));

    EXPECT_FALSE(non);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ok.unwrap().get(), u32(1));
}
