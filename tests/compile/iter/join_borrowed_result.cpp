import rstd;
using namespace rstd::prelude;
using namespace rstd::literals;
auto invalid() {
    auto key = [](const i32& x) {
        return x;
    };
    return rstd::iter::join_by(rstd::iter::once(1_i32),
                               rstd::iter::once(1_i32),
                               key,
                               key,
                               [](const i32& outer, const i32&) -> const i32& {
                                   return outer;
                               });
}
