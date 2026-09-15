import rstd;
using namespace rstd::prelude;

void rejects_implicit_move_from_borrow() {
    Vec<i32> values;
    auto     iterator  = rstd::iter::from_fn([&]() -> Option<Vec<i32>&> {
        return Some<Vec<i32>&>(values);
    });
    auto     flattened = rstd::move(iterator).flatten();
}
