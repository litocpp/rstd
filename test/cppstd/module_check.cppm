export module rstd.test.cppstd_module_check;

import rstd.cppstd;

namespace
{

constexpr auto HasCompleteCppStdSurface() -> bool {
    auto values = std::array<std::string, 2> { "beta", "alpha" };
    std::sort(values.begin(), values.end());
    return values[0] == "alpha" && values[1] == "beta";
}

static_assert(HasCompleteCppStdSurface());

} // namespace
