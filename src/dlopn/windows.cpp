module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
module rstd.dlopn;

#if RSTD_OS_WINDOWS
namespace rstd::dlopn
{
using namespace rstd::literals;

auto windows_error(const char* operation) -> WindowsError {
    auto code = rstd::uint32_t(::GetLastError());
    return WindowsError(rstd::format("{} failed ({})", operation, code), Some(code));
}

WindowsLibrary::WindowsLibrary(WindowsLibrary&& other) noexcept
    : handle_(rstd::exchange(other.handle_, nullptr)) {
}

auto WindowsLibrary::operator=(WindowsLibrary&& other) noexcept -> WindowsLibrary& {
    if (this != &other) {
        close();
        handle_ = rstd::exchange(other.handle_, nullptr);
    }
    return *this;
}

WindowsLibrary::~WindowsLibrary() {
    close();
}

auto WindowsLibrary::open(ref<ffi::CStr> filename) -> Result<WindowsLibrary, WindowsError> {
    auto text = filename.to_str();
    if (text.is_err()) return Err(WindowsError(String::make("invalid UTF-8 library path"_str)));
    auto name = ffi::OsString::from(text.unwrap_unchecked());
    return open_native(name.as_os_str());
}

auto WindowsLibrary::open_native(ref<ffi::OsStr> filename, rstd::uint32_t flags)
    -> Result<WindowsLibrary, WindowsError> {
    auto         units = os::windows::ffi::OsStrExt::encode_wide(filename);
    Vec<wchar_t> path;
    while (auto unit = units.next()) {
        if (*unit == u16(0))
            return Err(WindowsError(String::make("library path contains NUL"_str)));
        path.push(static_cast<wchar_t>(unit->to_primitive()));
    }
    path.push(L'\0');
    auto handle = ::LoadLibraryExW(path.as_slice().as_raw_ptr(), nullptr, flags);
    if (! handle) return Err(windows_error("LoadLibraryExW"));
    return Ok(WindowsLibrary(handle));
}

auto WindowsLibrary::symbol_raw(ref<ffi::CStr> name) const -> Result<void*, WindowsError> {
    auto address = ::GetProcAddress(static_cast<HMODULE>(handle_), name.as_ptr());
    if (! address) return Err(windows_error("GetProcAddress"));
    static_assert(sizeof(address) == sizeof(void*));
    void* result {};
    rstd::mem::memcpy(&result, &address, usize(sizeof(result)));
    return Ok(result);
}

void WindowsLibrary::close() noexcept {
    if (handle_) ::FreeLibrary(static_cast<HMODULE>(handle_));
    handle_ = nullptr;
}
} // namespace rstd::dlopn
#endif
