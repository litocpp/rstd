#include <gtest/gtest.h>

import rstd;

using namespace rstd::literals;

auto rstd_test_cross_tu_str() -> rstd::ref<rstd::str>;
auto rstd_test_cross_tu_bytes() -> rstd::slice<rstd::u8>;

TEST(StrLiteral, StaticStorageIsStableAcrossTranslationUnits) {
    auto text  = rstd_test_cross_tu_str();
    auto bytes = rstd_test_cross_tu_bytes();

    EXPECT_EQ(text, "cross-tu"_str);
    EXPECT_EQ(bytes, "cross-tu"_bytes);
    EXPECT_EQ(text.data(), "cross-tu"_str.data());
    EXPECT_EQ(bytes.as_raw_ptr(), "cross-tu"_bytes.as_raw_ptr());
}
