module rstd.core;
import :basic;
import :fmt;

namespace rstd
{
void rstd_assert(bool ok, const source_location) {
    // TODO: print
    if (! ok) cppstd::abort();
}

void panic_raw(cppstd::string_view msg, const source_location) {
    cppstd::fwrite(msg.data(), msg.size(), 1, stderr);
    cppstd::fflush(stderr);
    cppstd::abort();
}
} // namespace rstd
