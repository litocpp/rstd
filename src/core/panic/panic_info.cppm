export module rstd.core:panic_info;
export import :str;

namespace rstd::panic_
{
export struct PanicInfo {
    ref<str> message;

    cppstd::source_location location;
    // bool can_unwind;
    // bool force_no_backtrace;
};

} // namespace rstd::panic_