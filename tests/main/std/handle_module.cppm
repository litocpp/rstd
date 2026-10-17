module;
#include <rstd/macro.hpp>
export module rstd.tests.std_handle_module;
import rstd.os.handle;

export auto std_handle_module() -> bool {
#if RSTD_OS_WINDOWS
    rstd::os::handle::OwnedHandle empty;
    auto                          moved = rstd::move(empty);
    return ! empty.is_open() && ! moved.is_open();
#else
    return true;
#endif
}
