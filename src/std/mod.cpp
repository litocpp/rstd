module rstd;
import :process;
import rstd.core;

using rstd::panic_::PanicInfo;
using namespace rstd;

extern "C" [[noreturn]]
void rstd_panic_impl(PanicInfo const& info) {
    // TODO: unwind

    auto& loc = info.location;

    auto out = fmt::format("aborting due to panic at {}({}:{}):\n{}\n",
                           str_::extract_last(loc.file_name(), 2),
                           loc.function_name(),
                           loc.line(),
                           info.message);

    cppstd::fwrite(out.as_ref().as_raw_ptr(), out.as_ref().count_bytes(), 1, stderr);
    cppstd::fflush(stderr);
    process::abort();
}
