module;
#include <cstdlib>
#include <string_view>
#include <format>
#include <cstdio>
module rstd.core;
import :basic;
import :fmt;

namespace rstd
{
void rstd_assert(bool ok, const source_location) {
    // TODO: print
    if (! ok) std::abort();
}

void panic_raw(std::string_view msg, const source_location) {
    std::fwrite(msg.data(), msg.size(), 1, stderr);
    std::fflush(stderr);
    std::abort();
}
} // namespace rstd
