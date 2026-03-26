module rstd.core;
import :core;
import :fmt;
import :assert;

using rstd::panic_::PanicInfo;

extern "C" [[noreturn]]
void rstd_panic_impl(PanicInfo const&);

namespace rstd
{

void panic_fmt(fmt::Arguments args, const source_location loc) {
    auto info = PanicInfo {
        .message  = args,
        .location = loc,
    };
    rstd_panic_impl(info);
}

} // namespace rstd

namespace rstd::fmt
{

auto Formatter::write_fmt(Arguments args) -> bool {
    usize arg_idx = 0;
    const u8* p = args.fmt_ptr;
    const u8* end = args.fmt_ptr + args.fmt_len;
    const u8* last = p;

    while (p < end) {
        if (*p == '{') {
            if (p + 1 < end && *(p + 1) == '{') {
                // Escaped {{
                if (p > last) {
                    if (!write_raw(last, p - last)) return false;
                }
                if (!write_raw((const u8*)"{", 1)) return false;
                p += 2;
                last = p;
                continue;
            }
            // Start of placeholder
            if (p > last) {
                if (!write_raw(last, p - last)) return false;
            }
            // Find end of placeholder
            while (p < end && *p != '}') {
                p++;
            }
            if (p >= end) {
                return false;
            }
            // p is at '}'
            if (arg_idx < args.args_len) {
                if (!args.args_ptr[arg_idx].fmt(*this)) return false;
                arg_idx++;
            } else {
                return false;
            }
            p++;
            last = p;
        } else if (*p == '}') {
            if (p + 1 < end && *(p + 1) == '}') {
                // Escaped }}
                if (p > last) {
                    if (!write_raw(last, p - last)) return false;
                }
                if (!write_raw((const u8*)"}", 1)) return false;
                p += 2;
                last = p;
                continue;
            }
            return false;
        } else {
            p++;
        }
    }

    if (p > last) {
        if (!write_raw(last, p - last)) return false;
    }

    return true;
}

} // namespace rstd::fmt
