export module rstd.tests.cppstd_module_check;

import rstd.cppstd;

namespace
{

constexpr auto HasCompleteCppStdSurface() -> bool {
    auto values = std::array<std::string, 2> { "beta", "alpha" };
    std::sort(values.begin(), values.end());
    return values[0] == "alpha" && values[1] == "beta";
}

static_assert(HasCompleteCppStdSurface());

constexpr auto HasImportedIteratorSurface() -> bool {
    auto values   = std::vector<int> { 2, 3 };
    auto iterator = rstd::iter::from_range(values);
    auto first    = iterator.next();
    auto last     = iterator.next_back();
    return first.is_some() && last.is_some() && *first == 2 && *last == 3 &&
           iterator.next().is_none();
}

static_assert(HasImportedIteratorSurface());
static_assert(std::input_iterator<std::forward_list<int>::iterator>);
static_assert(requires(std::forward_list<int>& values) { rstd::iter::from_range(values); });

} // namespace
