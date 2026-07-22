#include <gtest/gtest.h>

import rstd.toml;

TEST(TomlModule, ImportsIndependentDecoder) {
    auto value = rstd::toml::from_str("ready = true\n").unwrap();
    auto ready = value.get("ready");
    ASSERT_TRUE(ready.is_some());
    EXPECT_EQ((**ready).as_bool(), rstd::Some(true));
}
