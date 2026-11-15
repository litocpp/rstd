module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#elif (RSTD_OS_LINUX || RSTD_OS_MACOS) && ! defined(__ANDROID__)
#include <errno.h>
#include <pwd.h>
#include <unistd.h>
#endif
#if RSTD_OS_MACOS
#include "native_apple.hpp"
#endif
module rstd;
import :dirs.roots;
import :env;
import :os.unix.ffi;
import :os.windows.ffi;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;
using rstd::path::PathBuf;

namespace rstd::dirs::sys
{

#if (RSTD_OS_LINUX || RSTD_OS_MACOS) && ! defined(__ANDROID__)
auto path_from_native(const char* value) -> PathBuf {
    return PathBuf::from(
        os::unix::ffi::OsStringExt::from_vec(Vec<u8>::from(ffi::CStr::from_ptr(value).to_bytes())));
}

auto detect_xdg_home() -> Result<Option<PathBuf>, Error> {
    auto configured = env::var_os("HOME"_str);
    if (configured.is_some()) {
        auto path = PathBuf::from(rstd::move(configured).unwrap());
        if (valid_absolute(path.as_path())) return Ok(Some(rstd::move(path)));
    }

    auto buffer = Vec<char>::make();
    buffer.resize(usize(1024), char {});
    const auto uid = ::getuid();
    while (true) {
        struct passwd  entry {};
        struct passwd* result = nullptr;
        auto           status =
            ::getpwuid_r(uid, &entry, buffer.as_mut_ptr(), buffer.len().to_primitive(), &result);
        if (status == ERANGE && buffer.len() <= usize::MAX / usize(2)) {
            buffer.resize(buffer.len() * usize(2), char {});
            continue;
        }
        if (status != 0) return Err(Error::system(io::Error::from_raw_os_error(i32(status))));
        if (result == nullptr || result->pw_dir == nullptr) return Ok(None());
        auto path = path_from_native(result->pw_dir);
        if (! valid_absolute(path.as_path())) return Ok(None());
        return Ok(Some(rstd::move(path)));
    }
}
#endif

auto detect_xdg_roots() -> Result<Roots, Error> {
#if (RSTD_OS_LINUX || RSTD_OS_MACOS) && ! defined(__ANDROID__)
    XdgEnvironment environment {
        .home        = None(),
        .config_home = env::var_os("XDG_CONFIG_HOME"_str),
        .data_home   = env::var_os("XDG_DATA_HOME"_str),
        .cache_home  = env::var_os("XDG_CACHE_HOME"_str),
        .state_home  = env::var_os("XDG_STATE_HOME"_str),
        .runtime_dir = env::var_os("XDG_RUNTIME_DIR"_str),
        .config_dirs = env::var_os("XDG_CONFIG_DIRS"_str),
        .data_dirs   = env::var_os("XDG_DATA_DIRS"_str),
    };
    auto home = detect_xdg_home();
    if (home.is_err()) return Err(rstd::move(home).unwrap_err());
    environment.home = rstd::move(home).unwrap();
    return Ok(resolve_xdg(rstd::move(environment)));
#else
    return Err(Error::unsupported());
#endif
}

#if RSTD_OS_WINDOWS
auto known_folder(REFKNOWNFOLDERID id) -> Result<Option<PathBuf>, Error> {
    struct FolderPath {
        PWSTR value {};
        ~FolderPath() { ::CoTaskMemFree(value); }
    } path;
    auto status = ::SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &path.value);
    if (FAILED(status))
        return Err(Error::platform(String::make("SHGetKnownFolderPath failed"_str), i64(status)));
    if (path.value == nullptr || *path.value == L'\0') return Ok(None());
    auto units = Vec<u16>::make();
    for (auto cursor = path.value; *cursor != L'\0'; ++cursor)
        units.push(u16(static_cast<unsigned short>(*cursor)));
    auto result = PathBuf::from(os::windows::ffi::OsStringExt::from_wide(units.as_slice()));
    if (! valid_absolute(result.as_path()))
        return Err(Error::platform(String::make("Known Folder path is not absolute"_str), i64()));
    return Ok(Some(rstd::move(result)));
}
#endif

#if RSTD_OS_MACOS
struct AppleRoots {
    Roots         roots;
    Option<Error> error;
};

auto receive_apple_root(void* context, AppleDirectory directory, const char* value) -> bool {
    auto& detected = *static_cast<AppleRoots*>(context);
    if (value == nullptr) {
        detected.error = Some(Error::platform(
            String::make("Foundation directory has no file system representation"_str), i64()));
        return false;
    }
    auto path = path_from_native(value);
    if (! valid_absolute(path.as_path())) {
        detected.error =
            Some(Error::platform(String::make("Foundation directory is not absolute"_str), i64()));
        return false;
    }
    switch (directory) {
    case AppleDirectory::Home: detected.roots.home = Some(rstd::move(path)); break;
    case AppleDirectory::ApplicationSupport:
        detected.roots.config     = Some(path.clone());
        detected.roots.data       = Some(path.clone());
        detected.roots.data_local = Some(path.clone());
        detected.roots.state      = Some(rstd::move(path));
        break;
    case AppleDirectory::Cache: detected.roots.cache = Some(rstd::move(path)); break;
    case AppleDirectory::LocalApplicationSupport:
        push_unique(detected.roots.config_dirs, path.clone());
        push_unique(detected.roots.data_dirs, rstd::move(path));
        break;
    }
    return true;
}
#endif

auto detect_native_roots() -> Result<ResolvedRoots, Error> {
#if defined(__ANDROID__)
    return Err(Error::unsupported());
#elif RSTD_OS_WINDOWS
    auto home = known_folder(FOLDERID_Profile);
    if (home.is_err()) return Err(rstd::move(home).unwrap_err());
    auto roaming = known_folder(FOLDERID_RoamingAppData);
    if (roaming.is_err()) return Err(rstd::move(roaming).unwrap_err());
    auto local = known_folder(FOLDERID_LocalAppData);
    if (local.is_err()) return Err(rstd::move(local).unwrap_err());
    auto shared = known_folder(FOLDERID_ProgramData);
    if (shared.is_err()) return Err(rstd::move(shared).unwrap_err());

    Roots roots;
    roots.home       = rstd::move(home).unwrap();
    roots.config     = clone_path(*roaming);
    roots.data       = rstd::move(roaming).unwrap();
    roots.data_local = clone_path(*local);
    roots.cache      = clone_path(*local);
    roots.state      = rstd::move(local).unwrap();
    if (shared->is_some()) {
        roots.config_dirs.push((**shared).clone());
        roots.data_dirs.push(rstd::move(shared).unwrap().unwrap());
    }
    return Ok(ResolvedRoots { Layout::Windows, rstd::move(roots) });
#elif RSTD_OS_MACOS
    AppleRoots detected {};
    if (! query_apple_roots(&detected, receive_apple_root))
        return Err(rstd::move(detected.error).unwrap());
    return Ok(ResolvedRoots { Layout::Apple, rstd::move(detected.roots) });
#elif RSTD_OS_LINUX
    auto roots = detect_xdg_roots();
    if (roots.is_err()) return Err(rstd::move(roots).unwrap_err());
    return Ok(ResolvedRoots { Layout::Xdg, rstd::move(roots).unwrap() });
#else
    return Err(Error::unsupported());
#endif
}

} // namespace rstd::dirs::sys
