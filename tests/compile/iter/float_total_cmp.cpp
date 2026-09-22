import rstd;
using namespace rstd::prelude;

void rejects_partial_order_as_total() {
    auto order = rstd::iter::once(f64(1)).cmp(rstd::iter::once(f64(2)));
}
