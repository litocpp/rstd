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

template<typename T>
constexpr auto unchecked_nonzero_roundtrip() -> bool {
    if (NonZero<T>::make_unchecked(T(1)).get() != T(1)) return false;
    if (NonZero<T>::make_unchecked(T::MAX).get() != T::MAX) return false;
    if constexpr (T::MIN != T()) {
        if (NonZero<T>::make_unchecked(T::MIN).get() != T::MIN) return false;
    }
    return true;
}

template<typename... Ts>
constexpr auto unchecked_nonzero_roundtrips() -> bool {
    return (unchecked_nonzero_roundtrip<Ts>() && ...);
}

static_assert(
    unchecked_nonzero_roundtrips<u8, u16, u32, u64, u128, usize, i8, i16, i32, i64, i128, isize>());

TEST(NonZero, UncheckedRoundtrip) {
    EXPECT_TRUE((unchecked_nonzero_roundtrips<u8,
                                              u16,
                                              u32,
                                              u64,
                                              u128,
                                              usize,
                                              i8,
                                              i16,
                                              i32,
                                              i64,
                                              i128,
                                              isize>()));
}

TEST(NonZero, Basic) {
    auto non = NonZero<u32>::make(u32());
    auto ok  = NonZero<u32>::make(u32(1));

    EXPECT_FALSE(non);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ok.unwrap().get(), u32(1));
}
