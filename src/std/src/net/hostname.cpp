module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <windows.h>
#elif RSTD_OS_UNIX
#include <errno.h>
#include <unistd.h>
#endif
module rstd.net.hostname;

using namespace rstd::prelude;
using ::alloc::string::String;
using ::alloc::vec::Vec;
auto rstd::net::hostname() -> io::Result<String> {
    auto invalid = [] {
        return io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidData });
    };
#if RSTD_OS_WINDOWS
    wchar_t name[256] {};
    DWORD   length = 256;
    if (! ::GetComputerNameExW(ComputerNameDnsHostname, name, &length))
        return Err(io::error::Error::from_raw_os_error(i32(::GetLastError())));
    char buffer[1024];
    auto count = ::WideCharToMultiByte(CP_UTF8,
                                       WC_ERR_INVALID_CHARS,
                                       name,
                                       static_cast<int>(length),
                                       buffer,
                                       sizeof(buffer),
                                       nullptr,
                                       nullptr);
    if (count == 0) return Err(io::error::Error::from_raw_os_error(i32(::GetLastError())));
    auto bytes = Vec<u8>::make();
    for (int i = 0; i < count; ++i) bytes.push(u8(static_cast<unsigned char>(buffer[i])));
#elif RSTD_OS_UNIX
    char buffer[256] {};
    if (::gethostname(buffer, sizeof(buffer)) != 0)
        return Err(io::error::Error::from_raw_os_error(i32(errno)));
    if (buffer[sizeof(buffer) - 1] != 0) return Err(invalid());
    auto bytes = Vec<u8>::make();
    for (char c : buffer) {
        if (c == 0) break;
        bytes.push(u8(static_cast<unsigned char>(c)));
    }
#else
    return Err(
        io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
#endif
#if RSTD_OS_WINDOWS || RSTD_OS_UNIX
    auto text = rstd::str_::from_utf8(bytes.as_slice());
    if (text.is_err()) return Err(invalid());
    return Ok(String::make(*text));
#endif
}
