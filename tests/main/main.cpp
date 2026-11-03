#include <rstd/macro.hpp>

import rstd;
import rstd.test;

using namespace rstd::literals;

auto toml_conformance_decode() -> int;
#if RSTD_OS_UNIX || RSTD_OS_WINDOWS
auto signal_test_mode(int argc, char** argv) -> int;
#endif

int main(int argc, char** argv) {
#if RSTD_OS_UNIX || RSTD_OS_WINDOWS
    auto signal_result = signal_test_mode(argc, argv);
    if (signal_result >= 0) return signal_result;
#endif
    rstd::env::args_init(argc, argv);
    auto arguments = rstd::env::args();
    (void)arguments.next();
    auto mode = arguments.next();
    if (mode.is_some() && mode->as_str() == "--decoder"_str) return toml_conformance_decode();
    return rstd::test::run_registered().to_primitive();
}
