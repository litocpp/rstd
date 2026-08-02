#include <rstd/test/gtest.hpp>

import rstd.toml;

using namespace rstd::literals;

TEST(TomlModule, ImportsIndependentDecoder) {
    auto value = rstd::toml::from_str("ready = true\n"_str).unwrap();
    auto ready = value.get("ready"_str);
    ASSERT_TRUE(ready.is_some());
    EXPECT_EQ((**ready).as_bool(), rstd::Some(true));
}
