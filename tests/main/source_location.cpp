#include <cstring>
#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;

static_assert(rstd::mtp::is_standard_layout<source_location>);
static_assert(rstd::mtp::triv_copyable<source_location>);

TEST(SourceLocation, DefaultConstructionIsEmpty) {
    constexpr auto location = source_location {};
    static_assert(location.file_name()[0] == '\0');
    static_assert(location.function_name()[0] == '\0');
    static_assert(location.line() == 0);
    static_assert(location.column() == 0);
}

TEST(SourceLocation, CurrentCapturesCallSite) {
    constexpr auto expected_line = __LINE__ + 1;
    constexpr auto location      = source_location::current();
    static_assert(location.line() == expected_line);
    static_assert(location.column() > 0);
    EXPECT_NE(std::strstr(location.function_name(), "CurrentCapturesCallSite"), nullptr);
    EXPECT_NE(std::strstr(location.file_name(), "source_location.cpp"), nullptr);
}
