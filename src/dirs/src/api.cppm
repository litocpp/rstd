module;
#include <rstd/macro.hpp>
export module rstd.dirs:api;
export import :types;
import :roots;

using rstd::path::Path;
using rstd::path::PathBuf;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd::dirs
{
class BaseDirs {
    sys::ResolvedRoots value_;
    explicit BaseDirs(sys::ResolvedRoots value): value_(rstd::move(value)) {}
    friend auto sys::base_dirs(sys::ResolvedRoots) -> BaseDirs;
    friend class ProjectDirs;

public:
    // Captures locations without creating directories or checking their accessibility.
    static auto detect() -> Result<BaseDirs, Error> {
        return Ok(BaseDirs(rstd_try(sys::detect_native_roots())));
    }
    static auto detect_xdg() -> Result<BaseDirs, Error> {
        return Ok(BaseDirs({ sys::Layout::Xdg, rstd_try(sys::detect_xdg_roots()) }));
    }
    auto home_dir() const noexcept -> Option<ref<Path>> {
        return sys::borrow_path(value_.roots.home);
    }
    auto config_dir() const noexcept -> Option<ref<Path>> {
        return sys::borrow_path(value_.roots.config);
    }
    auto data_dir() const noexcept -> Option<ref<Path>> {
        return sys::borrow_path(value_.roots.data);
    }
    auto data_local_dir() const noexcept -> Option<ref<Path>> {
        return sys::borrow_path(value_.roots.data_local);
    }
    auto cache_dir() const noexcept -> Option<ref<Path>> {
        return sys::borrow_path(value_.roots.cache);
    }
    auto state_dir() const noexcept -> Option<ref<Path>> {
        return sys::borrow_path(value_.roots.state);
    }
    // A runtime location is not a guarantee of secure ownership or permissions.
    auto runtime_dir() const noexcept -> Option<ref<Path>> {
        return sys::borrow_path(value_.roots.runtime);
    }
    auto config_dirs() const noexcept -> slice<PathBuf> {
        return value_.roots.config_dirs.as_slice();
    }
    auto data_dirs() const noexcept -> slice<PathBuf> { return value_.roots.data_dirs.as_slice(); }
    auto config_search_dirs() const -> Vec<PathBuf> {
        return sys::search_paths(value_.roots.config, config_dirs());
    }
    auto data_search_dirs() const -> Vec<PathBuf> {
        return sys::search_paths(value_.roots.data, data_dirs());
    }
};

class ProjectDirs {
    AppRoots     roots_;
    Vec<PathBuf> config_dirs_, data_dirs_;
    explicit ProjectDirs(AppRoots roots): roots_(rstd::move(roots)) {}

public:
    static auto from(const BaseDirs& base, const AppId& id) -> ProjectDirs {
        const auto& source = base.value_.roots;
        auto        layout = base.value_.layout;
        auto        append = [&](const Option<PathBuf>& root, ref<str> suffix) -> Option<PathBuf> {
            return root.as_ref().map([&](const PathBuf& path) {
                auto result = path.join(id.as_str());
                if (! suffix.is_empty()) result.push(suffix);
                return result;
            });
        };
        AppRoots roots;
        roots.config =
            append(source.config, layout == sys::Layout::Windows ? "config"_str : ""_str);
        roots.data = append(source.data, layout == sys::Layout::Windows ? "data"_str : ""_str);
        roots.data_local =
            append(source.data_local, layout == sys::Layout::Windows ? "data"_str : ""_str);
        roots.cache   = append(source.cache, layout == sys::Layout::Windows ? "cache"_str : ""_str);
        roots.state   = append(source.state, layout == sys::Layout::Xdg ? ""_str : "state"_str);
        roots.runtime = append(source.runtime, ""_str);
        ProjectDirs result(rstd::move(roots));
        auto        system_paths = [&](slice<PathBuf> paths, ref<str> suffix) {
            Vec<PathBuf> output;
            for (const auto& root : paths) {
                auto path = root.join(id.as_str());
                if (! suffix.is_empty()) path.push(suffix);
                sys::push_unique(output, rstd::move(path));
            }
            return output;
        };
        result.config_dirs_ = system_paths(base.config_dirs(),
                                           layout == sys::Layout::Windows ? "config"_str : ""_str);
        result.data_dirs_ =
            system_paths(base.data_dirs(), layout == sys::Layout::Windows ? "data"_str : ""_str);
        return result;
    }

    // Host-provided roots are already application-specific.
    static auto from_roots(AppRoots roots) -> Result<ProjectDirs, Error> {
        for (const auto* path : { &roots.config,
                                  &roots.data,
                                  &roots.data_local,
                                  &roots.cache,
                                  &roots.state,
                                  &roots.runtime })
            if (path->is_some() && ! sys::valid_absolute((*path)->as_path()))
                return Err(Error::InvalidPath((*path)->clone()));
        return Ok(ProjectDirs(rstd::move(roots)));
    }

    auto config_dir() const noexcept -> Option<ref<Path>> {
        return sys::borrow_path(roots_.config);
    }
    auto data_dir() const noexcept -> Option<ref<Path>> { return sys::borrow_path(roots_.data); }
    auto data_local_dir() const noexcept -> Option<ref<Path>> {
        return sys::borrow_path(roots_.data_local);
    }
    auto cache_dir() const noexcept -> Option<ref<Path>> { return sys::borrow_path(roots_.cache); }
    auto state_dir() const noexcept -> Option<ref<Path>> { return sys::borrow_path(roots_.state); }
    auto runtime_dir() const noexcept -> Option<ref<Path>> {
        return sys::borrow_path(roots_.runtime);
    }
    auto config_dirs() const noexcept -> slice<PathBuf> { return config_dirs_.as_slice(); }
    auto data_dirs() const noexcept -> slice<PathBuf> { return data_dirs_.as_slice(); }
    auto config_search_dirs() const -> Vec<PathBuf> {
        return sys::search_paths(roots_.config, config_dirs());
    }
    auto data_search_dirs() const -> Vec<PathBuf> {
        return sys::search_paths(roots_.data, data_dirs());
    }
};
} // namespace rstd::dirs

namespace rstd::dirs::sys
{
auto base_dirs(ResolvedRoots roots) -> BaseDirs {
    return BaseDirs(rstd::move(roots));
}
} // namespace rstd::dirs::sys
