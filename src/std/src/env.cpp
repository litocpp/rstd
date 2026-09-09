module;
#include <rstd/macro.hpp>
module rstd;
import :env;
import :sys.pal;
import :sys.libc;
import :sys.os_str.windows;
import :sys.args.windows;

using namespace rstd::prelude;
using ::alloc::vec::Vec;
using ::alloc::ffi::CString;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

namespace rstd::env
{

auto valid_key(ref<OsStr> value) -> bool {
    if (value.is_empty()) return false;
    for (auto byte : value.as_encoded_bytes())
        if (byte == u8() || byte == u8('=')) return false;
    return true;
}

auto var_os(ref<OsStr> key) -> Option<OsString> {
    for (auto byte : key.as_encoded_bytes())
        if (byte == u8()) return None();
#if RSTD_OS_WINDOWS
    auto name = sys::os_str::windows::to_wide(key);
    if (name.is_err()) return None();
    auto buffer = Vec<wchar_t>::with_capacity(usize(256));
    buffer.resize(usize(256), wchar_t {});
    while (true) {
        sys::libc::SetLastError(0);
        auto count = sys::libc::GetEnvironmentVariableW(
            name->as_ptr(),
            buffer.as_mut_ptr(),
            static_cast<unsigned long>(buffer.len().to_primitive()));
        if (count == 0) {
            if (sys::libc::GetLastError() != 0) return None();
            return Some(OsString::make());
        }
        if (count < buffer.len().to_primitive())
            return Some(sys::os_str::windows::from_wide(buffer.as_ptr(), usize(count)));
        buffer.resize(usize(count), wchar_t {});
    }
#else
    auto  name  = CString::make(Vec<u8>::from(key.as_encoded_bytes())).unwrap();
    auto* value = sys::pal::getenv_internal(name.as_ptr());
    return value == nullptr ? None() : Some(os_string_from_cstr(value));
#endif
}

void set_var(ref<OsStr> key, ref<OsStr> value) {
    if (! valid_key(key)) rstd::panic { "invalid environment variable name" };
#if RSTD_OS_WINDOWS
    auto name    = sys::os_str::windows::to_wide(key).unwrap();
    auto content = sys::os_str::windows::to_wide(value).unwrap();
    if (! sys::libc::SetEnvironmentVariableW(name.as_ptr(), content.as_ptr()))
        rstd::panic { "cannot set environment variable" };
#else
    auto name    = CString::make(Vec<u8>::from(key.as_encoded_bytes())).unwrap();
    auto content = CString::make(Vec<u8>::from(value.as_encoded_bytes())).unwrap();
    if (! sys::pal::setenv_internal(name.as_ptr(), content.as_ptr()))
        rstd::panic { "cannot set environment variable" };
#endif
}

void remove_var(ref<OsStr> key) {
    if (! valid_key(key)) rstd::panic { "invalid environment variable name" };
#if RSTD_OS_WINDOWS
    auto name = sys::os_str::windows::to_wide(key).unwrap();
    if (! sys::libc::SetEnvironmentVariableW(name.as_ptr(), nullptr))
        rstd::panic { "cannot remove environment variable" };
#else
    auto name = CString::make(Vec<u8>::from(key.as_encoded_bytes())).unwrap();
    if (! sys::pal::unsetenv_internal(name.as_ptr()))
        rstd::panic { "cannot remove environment variable" };
#endif
}

auto vars_os() -> VarsOs {
    auto values = Vec<tuple<OsString, OsString>>::make();
#if RSTD_OS_WINDOWS
    auto block = sys::libc::GetEnvironmentStringsW();
    if (block == nullptr) rstd::panic { "cannot read environment" };
    for (auto cursor = block; *cursor != L'\0';) {
        auto length = rstd::size_t {};
        while (cursor[length] != L'\0') ++length;
        auto separator = rstd::size_t(1);
        while (separator < length && cursor[separator] != L'=') ++separator;
        if (separator < length)
            values.push(tuple<OsString, OsString>(
                sys::os_str::windows::from_wide(cursor, usize(separator)),
                sys::os_str::windows::from_wide(cursor + separator + 1,
                                                usize(length - separator - 1))));
        cursor += length + 1;
    }
    (void)sys::libc::FreeEnvironmentStringsW(block);
#else
    for (auto cursor = sys::libc::environ; cursor != nullptr && *cursor != nullptr; ++cursor) {
        auto item  = os_string_from_cstr(*cursor);
        auto split = item.as_os_str().split_once(u8('='));
        if (split.is_some() && ! split->template get<0>().is_empty())
            values.push(tuple<OsString, OsString>(split->template get<0>().to_os_string(),
                                                  split->template get<1>().to_os_string()));
    }
#endif
    return rstd::move(values).into_iter();
}

auto args_os() -> ArgsOs {
#if RSTD_OS_WINDOWS
    auto raw   = sys::libc::GetCommandLineW();
    auto units = Vec<u16>::make();
    if (raw != nullptr)
        for (auto cursor = raw; *cursor != L'\0'; ++cursor)
            units.push(u16(static_cast<unsigned>(*cursor)));
    auto parsed = sys::args::windows::parse(units.as_slice());
    auto values = Vec<OsString>::with_capacity(parsed.len());
    for (auto& item : parsed) values.push(OsString::from_encoded_bytes_unchecked(rstd::move(item)));
    if (values.is_empty()) {
        auto buffer = Vec<wchar_t>::with_capacity(usize(256));
        buffer.resize(usize(256), wchar_t {});
        while (true) {
            auto count = sys::libc::GetModuleFileNameW(
                nullptr,
                buffer.as_mut_ptr(),
                static_cast<unsigned long>(buffer.len().to_primitive()));
            if (count == 0) {
                values.push(OsString::make());
                break;
            }
            if (count < buffer.len().to_primitive()) {
                values.push(sys::os_str::windows::from_wide(buffer.as_ptr(), usize(count)));
                break;
            }
            buffer.resize(buffer.len() * usize(2), wchar_t {});
        }
    }
#else
    auto raw    = sys::pal::args_argc_argv();
    auto length = raw.argc < 0 ? rstd::size_t(0) : static_cast<rstd::size_t>(raw.argc);
    auto values = Vec<OsString>::with_capacity(usize(length));
    for (rstd::size_t index = 0; index < length; ++index) {
        if (raw.argv[index] == nullptr) break;
        values.push(os_string_from_cstr(raw.argv[index]));
    }
#endif
    return rstd::move(values).into_iter();
}

auto args() -> Args {
    return Args(args_os());
}

void args_init(int argc, char const* const* argv) {
#if RSTD_OS_WINDOWS
    (void)argc;
    (void)argv;
#else
    sys::pal::args_capture(argc, argv);
#endif
}

} // namespace rstd::env
