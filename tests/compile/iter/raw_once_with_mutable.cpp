import rstd.core;
using namespace rstd::prelude;
namespace iter = rstd::iter;

void rejects_raw_item() {
    int  value  = 1;
    auto source = iter::once_with([&]() -> int& {
        return value;
    });
    (void)source;
}
