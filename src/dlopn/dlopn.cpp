module rstd.dlopn;

namespace rstd::dlopn
{
namespace
{

auto error_message(const char* message, const char* fallback) -> Error {
    if (message) {
        auto text = ffi::CStr::from_ptr(message).to_str();
        if (text.is_ok()) return Error(String::make(text.unwrap_unchecked()));
    }
    return Error(String::make(ffi::CStr::from_ptr(fallback).to_str().unwrap_unchecked()));
}

} // namespace

Library::Library(Library&& other) noexcept: handle_(other.handle_) {
    other.handle_ = nullptr;
}

auto Library::operator=(Library&& other) noexcept -> Library& {
    if (this != &other) {
        close();
        handle_       = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

Library::~Library() {
    close();
}

auto Library::open(ref<ffi::CStr> filename, int flags) -> Result<Library, Error> {
    dlerror();
    void* handle = dlopen(filename.as_ptr(), flags);
    if (! handle) return Err(error_message(dlerror(), "dlopen failed"));
    return Ok(Library(handle));
}

auto Library::symbol_raw(ref<ffi::CStr> name) const -> Result<void*, Error> {
    dlerror();
    void*       address = dlsym(handle_, name.as_ptr());
    const char* error   = dlerror();
    if (error) return Err(error_message(error, "dlsym failed"));
    return Ok(address);
}

void Library::close() noexcept {
    if (handle_) dlclose(handle_);
    handle_ = nullptr;
}

} // namespace rstd::dlopn
