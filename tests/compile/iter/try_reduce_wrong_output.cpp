import rstd.core;
using namespace rstd::prelude;
using namespace rstd::literals;
auto invalid() {
    return rstd::iter::once(1_i32).try_reduce([](i32, i32) {
        return Some(1_i64);
    });
}
