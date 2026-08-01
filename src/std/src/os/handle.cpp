module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <windows.h>
#endif

module rstd;
import :os.handle;

#if RSTD_OS_WINDOWS
namespace rstd::os::handle
{

void OwnedHandle::close_() noexcept {
    if (handle_ == INVALID_RAW_HANDLE) return;
    ::CloseHandle(handle_);
    handle_ = INVALID_RAW_HANDLE;
}

OwnedHandle::~OwnedHandle() {
    close_();
}

} // namespace rstd::os::handle
#endif
