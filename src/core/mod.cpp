module rstd.core;
import :core;
import :fmt;
import :assert;

using rstd::panic_::PanicInfo;

extern "C" [[noreturn]]
void rstd_panic_impl(PanicInfo const&);

namespace rstd
{

void panic_fmt(ref<str> msg, const source_location loc) {
    auto info = PanicInfo {
        .message  = msg,
        .location = loc,
    };
    rstd_panic_impl(info);
}

} // namespace rstd
