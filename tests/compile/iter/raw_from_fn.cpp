import rstd.core;
using namespace rstd::prelude;
namespace iter = rstd::iter;

void rejects_raw_item() {
    int  value  = 1;
    auto source = iter::from_fn([&] {
        return Some<int&>(value);
    });
    (void)source;
}
