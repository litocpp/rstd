module;
#include <rstd/macro.hpp>
export module rstd:sys.os_str.windows;
import :os.windows.ffi;
import :ffi.os_str;
import :io.error;
import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::vec::Vec;

#if RSTD_OS_WINDOWS
namespace rstd::sys::os_str::windows
{

auto to_wide(ref<ffi::OsStr> value, bool terminate = true) -> rstd::io::Result<Vec<wchar_t>> {
    auto output = Vec<wchar_t>::with_capacity(value.len() + usize(1));
    auto units  = os::windows::ffi::OsStrExt::encode_wide(value);
    while (auto unit = units.next()) {
        if (*unit == u16())
            return Err(rstd::io::error::Error::from_kind(
                rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::InvalidInput }));
        output.push(static_cast<wchar_t>(unit->to_primitive()));
    }
    if (terminate) output.push(L'\0');
    return Ok(rstd::move(output));
}

auto from_wide(const wchar_t* value, usize length) -> ffi::OsString {
    auto units = Vec<u16>::with_capacity(length);
    for (auto index = usize(); index < length; ++index)
        units.push(u16(static_cast<unsigned>(value[index.to_primitive()])));
    return os::windows::ffi::OsStringExt::from_wide(units.as_slice());
}

} // namespace rstd::sys::os_str::windows
#endif
