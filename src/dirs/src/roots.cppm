export module rstd.dirs:roots;
export import :types;

using rstd::ffi::OsString;
using rstd::path::Path;
using rstd::path::PathBuf;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd::dirs
{
class BaseDirs;
namespace sys
{
enum class Layout
{
    Xdg,
    Windows,
    Apple
};

struct Roots {
    Option<PathBuf> home, config, data, data_local, cache, state, runtime;
    Vec<PathBuf>    config_dirs, data_dirs;
};

struct ResolvedRoots {
    Layout layout;
    Roots  roots;
};

struct XdgEnvironment {
    Option<PathBuf>  home;
    Option<OsString> config_home, data_home, cache_home, state_home, runtime_dir, config_dirs,
        data_dirs;
};

auto valid_absolute(ref<Path> path) -> bool {
    if (! path.is_absolute()) return false;
    for (auto byte : path.as_os_str().as_encoded_bytes())
        if (byte == u8()) return false;
    return true;
}

auto borrow_path(const Option<PathBuf>& path) -> Option<ref<Path>> {
    return path.as_ref().map([](const PathBuf& value) {
        return value.as_path();
    });
}

auto clone_path(const Option<PathBuf>& path) -> Option<PathBuf> {
    return path.as_ref().map([](const PathBuf& value) {
        return value.clone();
    });
}

void push_unique(Vec<PathBuf>& paths, PathBuf path) {
    for (const auto& existing : paths)
        if (existing.as_path() == path.as_path()) return;
    paths.push(rstd::move(path));
}

auto search_paths(const Option<PathBuf>& user, slice<PathBuf> system) -> Vec<PathBuf> {
    Vec<PathBuf> paths;
    if (user.is_some()) push_unique(paths, user->clone());
    for (const auto& path : system) push_unique(paths, path.clone());
    return paths;
}

auto absolute_value(Option<OsString> value) -> Option<PathBuf> {
    if (value.is_none()) return None();
    auto path = PathBuf::from(rstd::move(*value));
    return valid_absolute(path.as_path()) ? Some(rstd::move(path)) : None();
}

auto xdg_home(Option<OsString> value, const Option<PathBuf>& home, ref<str> fallback)
    -> Option<PathBuf> {
    auto configured = absolute_value(rstd::move(value));
    if (configured.is_some()) return configured;
    return home.as_ref().map([&](const PathBuf& root) {
        return root.join(fallback);
    });
}

auto xdg_directories(Option<OsString> value, initializer_list<ref<str>> defaults) -> Vec<PathBuf> {
    Vec<PathBuf> paths;
    if (value.is_some()) {
        for (auto path : env::split_paths(value->as_os_str()))
            if (valid_absolute(path.as_path())) push_unique(paths, rstd::move(path));
    }
    if (paths.is_empty())
        for (auto path : defaults) paths.push(PathBuf::from(path));
    return paths;
}

auto resolve_xdg(XdgEnvironment values) -> Roots {
    Roots roots;
    if (values.home.is_some() && valid_absolute(values.home->as_path()))
        roots.home = rstd::move(values.home);
    roots.config      = xdg_home(rstd::move(values.config_home), roots.home, ".config"_str);
    roots.data        = xdg_home(rstd::move(values.data_home), roots.home, ".local/share"_str);
    roots.data_local  = clone_path(roots.data);
    roots.cache       = xdg_home(rstd::move(values.cache_home), roots.home, ".cache"_str);
    roots.state       = xdg_home(rstd::move(values.state_home), roots.home, ".local/state"_str);
    roots.runtime     = absolute_value(rstd::move(values.runtime_dir));
    roots.config_dirs = xdg_directories(rstd::move(values.config_dirs), { "/etc/xdg"_str });
    roots.data_dirs =
        xdg_directories(rstd::move(values.data_dirs), { "/usr/local/share"_str, "/usr/share"_str });
    return roots;
}

auto detect_xdg_roots() -> Result<Roots, Error>;
auto detect_native_roots() -> Result<ResolvedRoots, Error>;
auto base_dirs(ResolvedRoots roots) -> BaseDirs;
} // namespace sys
} // namespace rstd::dirs
