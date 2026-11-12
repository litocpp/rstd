import rstd.basic;

using namespace rstd::prelude;

constexpr auto initializer_list_works() -> bool {
    auto                       values = { 1, 2, 3 };
    std::initializer_list<int> standard { 1, 2, 3 };
    initializer_list<int>      prelude { 1, 2, 3 };
    int                        total = 0;
    for (auto value : { 1, 2, 3 }) total += value;
    return values.size() == 3 && standard.size() == 3 && prelude.size() == 3 && total == 6;
}

static_assert(initializer_list_works());
