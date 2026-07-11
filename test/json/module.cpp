#include <gtest/gtest.h>

import rstd.json;

TEST(JsonModule, ImportsIndependentlyFromStd) {
    auto value = rstd::json::from_str("{\"ready\":true}").unwrap();
    EXPECT_EQ(value["ready"], true);
}
