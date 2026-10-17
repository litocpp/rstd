module;
#include <rstd/macro.hpp>

export module rstd.tests.std_process_module;

import rstd.sys.process.linux;

export auto std_process_module_errors() -> bool {
#if RSTD_OS_LINUX
    auto const result = rstd::sys::process::pidfd_wait(-1, true);
    return ! result.available && result.error != 0 &&
           rstd::sys::process::pidfd_send_signal(-1, 0, false) != 0;
#else
    return true;
#endif
}
