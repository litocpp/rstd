module rstd.core;
import :core;
import :fmt;
import :assert;

using rstd::panic_::PanicInfo;

extern "C" [[noreturn]]
void rstd_panic_impl(PanicInfo const&);

namespace rstd
{

void assert_raw(ref<str> expr_str, ref<str> msg, const source_location loc) {
    auto out = cppstd::format("Assertion `{}` failed, on {}({}):{}{}{}\n",
                              expr_str,
                              str_::extract_last(loc.file_name(), 2),
                              loc.line(),
                              loc.function_name(),
                              msg ? "" : "\n",
                              msg);

    cppstd::fwrite(out.data(), out.size(), 1, stderr);
    cppstd::fflush(stderr);
    cppstd::abort();
}

void panic_raw(cppstd::string_view msg, const source_location) {
    cppstd::fwrite(msg.data(), msg.size(), 1, stderr);
    cppstd::fflush(stderr);
    cppstd::abort();
}

void panic_fmt(ref<str> msg, const source_location loc) {
    auto info = PanicInfo {
        .message  = msg,
        .location = loc,
    };
    rstd_panic_impl(info);
}

} // namespace rstd
