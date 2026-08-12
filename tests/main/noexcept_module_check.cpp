#include <rstd/test/gtest.hpp>

import rstd.tests.noexcept_module_check;

static_assert(noexcept(noexcept_module_check::no_throw_contract()));
static_assert(! noexcept(noexcept_module_check::throwing_contract()));

TEST(NoexceptModule, PreservesImportedExceptionSpecifications) {
    EXPECT_EQ(noexcept_module_check::no_throw_contract(), 1);
    EXPECT_EQ(noexcept_module_check::throwing_contract(), 2);
}
