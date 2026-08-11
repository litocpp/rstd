#include <rstd/test/gtest.hpp>

import rstd.json;

using namespace rstd::literals;

TEST(JsonModule, ImportsIndependentlyFromStd) {
    auto value = rstd::json::from_str("{\"ready\":true}"_str).unwrap();
    EXPECT_EQ(value["ready"_str], true);
}
