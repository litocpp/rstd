#include <cstring>
#include <rstd/test/gtest.hpp>

import rstd;
import rstd.tests.source_location_module_check;

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

TEST(SourceLocation, ImportedDefaultArgumentUsesConsumerCallSite) {
    constexpr auto expected_line = __LINE__ + 1;
    constexpr auto location      = source_location_module_check::capture();
    static_assert(location.line() == expected_line);
    EXPECT_NE(std::strstr(location.function_name(), "ImportedDefaultArgumentUsesConsumerCallSite"),
              nullptr);
    EXPECT_NE(std::strstr(location.file_name(), "source_location.cpp"), nullptr);
}

TEST(SourceLocation, ImportedTemplateUsesConsumerCallSite) {
    constexpr auto expected_line = __LINE__ + 1;
    constexpr auto location      = source_location_module_check::capture_template<int>();
    static_assert(location.line() == expected_line);
    EXPECT_NE(std::strstr(location.function_name(), "ImportedTemplateUsesConsumerCallSite"),
              nullptr);
}

TEST(SourceLocation, PanicAdapterUsesConsumerCallSite) {
    constexpr auto expected_line = __LINE__ + 1;
    constexpr auto location      = source_location_module_check::capture_panic();
    static_assert(location.line() == expected_line);
    EXPECT_NE(std::strstr(location.function_name(), "PanicAdapterUsesConsumerCallSite"), nullptr);
}

TEST(SourceLocation, HonorsPresumedLocation) {
    constexpr auto location = source_location_module_check::remapped;
    static_assert(location.line() == 700);
    EXPECT_EQ(std::strcmp(location.file_name(), "rstd-source-location-remapped.cpp"), 0);
}
