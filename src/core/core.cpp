module rstd.core;
import :core;
import :fmt;
import :assert;

namespace rstd
{
void assert_raw(ref<str> expr_str, ref<str> msg, const source_location loc) {
    auto out = rstd::format("{}:{}: {}: Assertion `{}` failed.{}{}\n",
                            loc.file_name(),
                            loc.line(),
                            loc.function_name(),
                            expr_str,
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
} // namespace rstd
