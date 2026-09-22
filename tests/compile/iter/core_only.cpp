import rstd.core;
using namespace rstd::prelude;
using namespace rstd::literals;
constexpr bool check() {
    const int values[] = { 1, 2, 3 };
    auto      view     = slice<int>::from_raw_parts(values, 3_usize);
    if (view.first()->get() != 1 || view.last()->get() != 3) return false;
    auto chunks = rstd::slice_::chunks(view, 2_usize);
    if (chunks.next()->len() != 2_usize) return false;
    auto result = rstd::iter::once(1_i32).try_find([](const i32&) {
        return Some(true);
    });
    return result.is_some() && result->is_some();
}
static_assert(check());
