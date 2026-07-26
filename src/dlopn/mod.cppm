module;
#include <dlfcn.h>
export module rstd.dlopn;
export import rstd;

export namespace rstd::dlopn
{

using namespace rstd::prelude;

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

} // namespace rstd::dlopn
