module;
#include <rstd/macro.hpp>
#if ! RSTD_OS_WINDOWS
#include <dlfcn.h>
#endif
export module rstd.dlopn;
export import rstd;

export namespace rstd::dlopn
{

using namespace rstd::prelude;

#if ! RSTD_OS_WINDOWS
using ::dlclose;
using ::dlerror;
using ::dlopen;
using ::dlsym;

inline constexpr int rtld_lazy    = RTLD_LAZY;
inline constexpr int rtld_now     = RTLD_NOW;
inline constexpr int rtld_global  = RTLD_GLOBAL;
inline constexpr int rtld_local   = RTLD_LOCAL;
inline void* const   rtld_default = RTLD_DEFAULT;
inline void* const   rtld_next    = RTLD_NEXT;

class Error {
public:
    explicit Error(String message) noexcept: message_(rstd::move(message)) {}

    auto message() const noexcept [[clang::lifetimebound]] -> ref<str> { return message_.as_str(); }

private:
    String message_;
};

class Library {
public:
    Library(const Library&)        = delete;
    auto operator=(const Library&) = delete;
    Library(Library&& other) noexcept;
    auto operator=(Library&& other) noexcept -> Library&;
    ~Library();

    static auto open(ref<ffi::CStr> filename, int flags = rtld_now | rtld_local)
        -> Result<Library, Error>;

    template<mtp::is_ptr T>
    auto symbol(ref<ffi::CStr> name) const -> Result<T, Error> {
        static_assert(sizeof(T) == sizeof(void*));
        auto result = symbol_raw(name);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());

        void* address = rstd::move(result).unwrap_unchecked();
        T     value {};
        rstd::mem::memcpy(&value, &address, usize(sizeof(address)));
        return Ok(value);
    }

private:
    explicit Library(void* handle) noexcept: handle_(handle) {}

    auto symbol_raw(ref<ffi::CStr> name) const -> Result<void*, Error>;
    void close() noexcept;

    void* handle_;
};
#else
class WindowsError {
public:
    explicit WindowsError(String message, Option<rstd::uint32_t> code = {}) noexcept
        : message_(rstd::move(message)), code_(code) {}

    auto message() const noexcept [[clang::lifetimebound]] -> ref<str> { return message_.as_str(); }
    auto raw_os_error() const noexcept -> Option<rstd::uint32_t> { return code_; }

private:
    String                 message_;
    Option<rstd::uint32_t> code_;
};

class WindowsLibrary {
public:
    WindowsLibrary(const WindowsLibrary&) = delete;
    auto operator=(const WindowsLibrary&) = delete;
    WindowsLibrary(WindowsLibrary&& other) noexcept;
    auto operator=(WindowsLibrary&& other) noexcept -> WindowsLibrary&;
    ~WindowsLibrary();

    // The C string path must be UTF-8.
    static auto open(ref<ffi::CStr> filename) -> Result<WindowsLibrary, WindowsError>;
    // Flags are passed to LoadLibraryExW; embedded NUL in the native path is rejected.
    static auto open_native(ref<ffi::OsStr> filename, rstd::uint32_t flags = 0)
        -> Result<WindowsLibrary, WindowsError>;

    template<mtp::is_ptr T>
    auto symbol(ref<ffi::CStr> name) const -> Result<T, WindowsError> {
        static_assert(sizeof(T) == sizeof(void*));
        auto result = symbol_raw(name);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        void* address = rstd::move(result).unwrap_unchecked();
        T     value {};
        rstd::mem::memcpy(&value, &address, usize(sizeof(address)));
        return Ok(value);
    }

private:
    explicit WindowsLibrary(void* handle) noexcept: handle_(handle) {}
    auto  symbol_raw(ref<ffi::CStr> name) const -> Result<void*, WindowsError>;
    void  close() noexcept;
    void* handle_;
};
#endif

} // namespace rstd::dlopn
