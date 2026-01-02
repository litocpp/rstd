module rstd.core;
import :core;
import :fmt;
import :assert;

namespace rstd
{
void assert_raw(ref<str> expr_str, ref<str> msg, const source_location) {
    cppstd::fwrite(expr_str.data(), expr_str.size(), 1, stderr);
    if (msg) {
        cppstd::fwrite(msg.data(), msg.size(), 1, stderr);
    }
    cppstd::fflush(stderr);
    cppstd::abort();
}

void panic_raw(cppstd::string_view msg, const source_location) {
    cppstd::fwrite(msg.data(), msg.size(), 1, stderr);
    cppstd::fflush(stderr);
    cppstd::abort();
}
} // namespace rstd
