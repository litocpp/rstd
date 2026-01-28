module rstd.core;
import :core;
import :fmt;
import :assert;

namespace rstd
{

namespace
{
constexpr auto extract_last(ref<str> path, usize count) -> ref<str> {
    auto size = path.size();
    while (size != 0) {
        if (path[size - 1] == '/' || path[size - 1] == '\\') {
            --count;
        }
        if (count != 0) {
            --size;
        } else {
            break;
        }
    }
    return ref<str>::from_raw_parts(path.begin() + size,  path.end() - path.begin());
}
} // namespace

void assert_raw(ref<str> expr_str, ref<str> msg, const source_location loc) {
    auto out = cppstd::format("Assertion `{}` failed, on {}({}):{}{}{}\n",
                            expr_str,
                            extract_last(loc.file_name(), 2),
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
} // namespace rstd
