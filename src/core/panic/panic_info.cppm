export module rstd.core:panic_info;
export import :fmt;
export import :str.str;

namespace rstd::panic_
{
export struct PanicInfo {
    fmt::Arguments message;

    cppstd::source_location location;
};

} // namespace rstd::panic_