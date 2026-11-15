module;
#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>

export module rstd:dirs_tests;
import :dirs;
import :dirs.roots;
import :env;
import :fs;
import :process;

#include "support.hpp"

namespace
{
using rstd::dirs::sys::Layout;
using rstd::dirs::sys::ResolvedRoots;
using rstd::dirs::sys::Roots;
using rstd::dirs::sys::XdgEnvironment;
namespace sys = rstd::dirs::sys;

auto separate_roots() -> Roots {
    Roots roots {
        .home       = Some(test_path("home"_str)),
        .config     = Some(test_path("config"_str)),
        .data       = Some(test_path("data"_str)),
        .data_local = Some(test_path("local"_str)),
        .cache      = Some(test_path("cache"_str)),
        .state      = Some(test_path("state"_str)),
        .runtime    = Some(test_path("runtime"_str)),
    };
    roots.config_dirs.push(test_path("system/config-b"_str));
    roots.config_dirs.push(test_path("system/config-a"_str));
    roots.data_dirs.push(test_path("system/data-b"_str));
    roots.data_dirs.push(test_path("system/data-a"_str));
    return roots;
}

auto expect_separate_base(const BaseDirs& base) -> void {
    expect_path(base.home_dir(), "home"_str);
    expect_path(base.config_dir(), "config"_str);
    expect_path(base.data_dir(), "data"_str);
    expect_path(base.data_local_dir(), "local"_str);
    expect_path(base.cache_dir(), "cache"_str);
    expect_path(base.state_dir(), "state"_str);
    expect_path(base.runtime_dir(), "runtime"_str);
    expect_paths(base.config_dirs(), { "system/config-b"_str, "system/config-a"_str });
    expect_paths(base.data_dirs(), { "system/data-b"_str, "system/data-a"_str });
}

#if RSTD_OS_UNIX
auto resolve(XdgEnvironment environment) -> BaseDirs {
    return sys::base_dirs(ResolvedRoots { Layout::Xdg, sys::resolve_xdg(rstd::move(environment)) });
}

auto env_path(ref<str> suffix) -> Option<OsString> {
    auto path = test_path(suffix);
    return Some(OsString::from(path.as_path().as_os_str()));
}

auto expect_default_system_roots(const BaseDirs& base) -> void {
    ASSERT_EQ(base.config_dirs().len(), usize(1));
    EXPECT_EQ(base.config_dirs()[usize()].as_path(), ref<Path>("/etc/xdg"_str));
    ASSERT_EQ(base.data_dirs().len(), usize(2));
    EXPECT_EQ(base.data_dirs()[usize()].as_path(), ref<Path>("/usr/local/share"_str));
    EXPECT_EQ(base.data_dirs()[usize(1)].as_path(), ref<Path>("/usr/share"_str));
}

auto expect_home_defaults(const BaseDirs& base) -> void {
    expect_path(base.home_dir(), "home"_str);
    expect_path(base.config_dir(), "home/.config"_str);
    expect_path(base.data_dir(), "home/.local/share"_str);
    expect_path(base.data_local_dir(), "home/.local/share"_str);
    expect_path(base.cache_dir(), "home/.cache"_str);
    expect_path(base.state_dir(), "home/.local/state"_str);
    EXPECT_TRUE(base.runtime_dir().is_none());
}

auto expect_independent_overrides(Option<PathBuf> home) -> void {
    auto base = resolve(XdgEnvironment {
        .home        = rstd::move(home),
        .config_home = env_path("explicit/config"_str),
        .data_home   = Some(OsString::from("relative-data"_str)),
        .state_home  = env_path("explicit/state"_str),
        .runtime_dir = env_path("explicit/runtime"_str),
    });
    EXPECT_TRUE(base.home_dir().is_none());
    expect_path(base.config_dir(), "explicit/config"_str);
    EXPECT_TRUE(base.data_dir().is_none());
    EXPECT_TRUE(base.data_local_dir().is_none());
    EXPECT_TRUE(base.cache_dir().is_none());
    expect_path(base.state_dir(), "explicit/state"_str);
    expect_path(base.runtime_dir(), "explicit/runtime"_str);
    expect_default_system_roots(base);
    auto project = ProjectDirs::from(base, AppId::parse("Qcm"_str).unwrap());
    expect_path(project.config_dir(), "explicit/config/Qcm"_str);
    expect_path(project.state_dir(), "explicit/state/Qcm"_str);
    expect_path(project.runtime_dir(), "explicit/runtime/Qcm"_str);
    EXPECT_TRUE(project.data_dir().is_none());
    EXPECT_TRUE(project.data_local_dir().is_none());
    EXPECT_TRUE(project.cache_dir().is_none());
}
#endif
} // namespace

TEST(DirsLayout, XdgAppendsOnlyTheApplicationId) {
    auto base = sys::base_dirs(ResolvedRoots { Layout::Xdg, separate_roots() });
    expect_separate_base(base);
    auto id      = AppId::parse("Qcm"_str).unwrap();
    auto project = ProjectDirs::from(base, id);
    expect_path(project.config_dir(), "config/Qcm"_str);
    expect_path(project.data_dir(), "data/Qcm"_str);
    expect_path(project.data_local_dir(), "local/Qcm"_str);
    expect_path(project.cache_dir(), "cache/Qcm"_str);
    expect_path(project.state_dir(), "state/Qcm"_str);
    expect_path(project.runtime_dir(), "runtime/Qcm"_str);
    expect_paths(project.config_dirs(), { "system/config-b/Qcm"_str, "system/config-a/Qcm"_str });
    expect_paths(project.data_dirs(), { "system/data-b/Qcm"_str, "system/data-a/Qcm"_str });
    auto config = project.config_search_dirs();
    auto data   = project.data_search_dirs();
    expect_paths(config.as_slice(),
                 { "config/Qcm"_str, "system/config-b/Qcm"_str, "system/config-a/Qcm"_str });
    expect_paths(data.as_slice(),
                 { "data/Qcm"_str, "system/data-b/Qcm"_str, "system/data-a/Qcm"_str });
    expect_separate_base(base);
}

TEST(DirsLayout, WindowsSeparatesRoamingLocalAndProgramData) {
    Roots roots {
        .home       = Some(test_path("profile"_str)),
        .config     = Some(test_path("roaming"_str)),
        .data       = Some(test_path("roaming"_str)),
        .data_local = Some(test_path("local"_str)),
        .cache      = Some(test_path("local"_str)),
        .state      = Some(test_path("local"_str)),
    };
    roots.config_dirs.push(test_path("program-data"_str));
    roots.data_dirs.push(test_path("program-data"_str));
    auto base = sys::base_dirs(ResolvedRoots { Layout::Windows, rstd::move(roots) });
    expect_path(base.home_dir(), "profile"_str);
    expect_path(base.config_dir(), "roaming"_str);
    expect_path(base.data_dir(), "roaming"_str);
    expect_path(base.data_local_dir(), "local"_str);
    expect_path(base.cache_dir(), "local"_str);
    expect_path(base.state_dir(), "local"_str);
    EXPECT_TRUE(base.runtime_dir().is_none());
    expect_paths(base.config_dirs(), { "program-data"_str });
    expect_paths(base.data_dirs(), { "program-data"_str });

    auto project = ProjectDirs::from(base, AppId::parse("Qcm"_str).unwrap());
    expect_path(project.config_dir(), "roaming/Qcm/config"_str);
    expect_path(project.data_dir(), "roaming/Qcm/data"_str);
    expect_path(project.data_local_dir(), "local/Qcm/data"_str);
    expect_path(project.cache_dir(), "local/Qcm/cache"_str);
    expect_path(project.state_dir(), "local/Qcm/state"_str);
    EXPECT_TRUE(project.runtime_dir().is_none());
    expect_paths(project.config_dirs(), { "program-data/Qcm/config"_str });
    expect_paths(project.data_dirs(), { "program-data/Qcm/data"_str });
    auto config = project.config_search_dirs();
    auto data   = project.data_search_dirs();
    expect_paths(config.as_slice(), { "roaming/Qcm/config"_str, "program-data/Qcm/config"_str });
    expect_paths(data.as_slice(), { "roaming/Qcm/data"_str, "program-data/Qcm/data"_str });
}

TEST(DirsLayout, AppleUsesApplicationSupportCachesAndExplicitStateChild) {
    Roots roots {
        .home       = Some(test_path("home"_str)),
        .config     = Some(test_path("Library/Application Support"_str)),
        .data       = Some(test_path("Library/Application Support"_str)),
        .data_local = Some(test_path("Library/Application Support"_str)),
        .cache      = Some(test_path("Library/Caches"_str)),
        .state      = Some(test_path("Library/Application Support"_str)),
    };
    roots.config_dirs.push(test_path("system/Library/Application Support"_str));
    roots.data_dirs.push(test_path("system/Library/Application Support"_str));
    auto base = sys::base_dirs(ResolvedRoots { Layout::Apple, rstd::move(roots) });
    expect_path(base.home_dir(), "home"_str);
    expect_path(base.config_dir(), "Library/Application Support"_str);
    expect_path(base.data_dir(), "Library/Application Support"_str);
    expect_path(base.data_local_dir(), "Library/Application Support"_str);
    expect_path(base.cache_dir(), "Library/Caches"_str);
    expect_path(base.state_dir(), "Library/Application Support"_str);
    EXPECT_TRUE(base.runtime_dir().is_none());
    expect_paths(base.config_dirs(), { "system/Library/Application Support"_str });
    expect_paths(base.data_dirs(), { "system/Library/Application Support"_str });

    auto project = ProjectDirs::from(base, AppId::parse("Qcm"_str).unwrap());
    expect_path(project.config_dir(), "Library/Application Support/Qcm"_str);
    expect_path(project.data_dir(), "Library/Application Support/Qcm"_str);
    expect_path(project.data_local_dir(), "Library/Application Support/Qcm"_str);
    expect_path(project.cache_dir(), "Library/Caches/Qcm"_str);
    expect_path(project.state_dir(), "Library/Application Support/Qcm/state"_str);
    EXPECT_TRUE(project.runtime_dir().is_none());
    expect_paths(project.config_dirs(), { "system/Library/Application Support/Qcm"_str });
    expect_paths(project.data_dirs(), { "system/Library/Application Support/Qcm"_str });
    auto config = project.config_search_dirs();
    auto data   = project.data_search_dirs();
    expect_paths(
        config.as_slice(),
        { "Library/Application Support/Qcm"_str, "system/Library/Application Support/Qcm"_str });
    expect_paths(
        data.as_slice(),
        { "Library/Application Support/Qcm"_str, "system/Library/Application Support/Qcm"_str });
}

TEST(DirsLayout, MissingDirectoriesStayMissingForEveryLayout) {
    for (auto layout : { Layout::Xdg, Layout::Windows, Layout::Apple }) {
        auto base = sys::base_dirs(ResolvedRoots { layout, Roots {} });
        EXPECT_TRUE(base.home_dir().is_none());
        EXPECT_TRUE(base.config_dir().is_none());
        EXPECT_TRUE(base.data_dir().is_none());
        EXPECT_TRUE(base.data_local_dir().is_none());
        EXPECT_TRUE(base.cache_dir().is_none());
        EXPECT_TRUE(base.state_dir().is_none());
        EXPECT_TRUE(base.runtime_dir().is_none());
        EXPECT_TRUE(base.config_dirs().is_empty());
        EXPECT_TRUE(base.data_dirs().is_empty());
        EXPECT_TRUE(base.config_search_dirs().is_empty());
        EXPECT_TRUE(base.data_search_dirs().is_empty());
        auto project = ProjectDirs::from(base, AppId::parse("Qcm"_str).unwrap());
        expect_missing(project);
    }
}

TEST(DirsLayout, MissingUserRootsDoNotHideSystemSearchRoots) {
    struct Case {
        Layout   layout;
        ref<str> config, data;
    };
    const Case cases[] = {
        { Layout::Xdg, "system/Qcm"_str, "system/Qcm"_str },
        { Layout::Windows, "system/Qcm/config"_str, "system/Qcm/data"_str },
        { Layout::Apple, "system/Qcm"_str, "system/Qcm"_str },
    };
    for (const auto& value : cases) {
        Roots roots {};
        roots.config_dirs.push(test_path("system"_str));
        roots.data_dirs.push(test_path("system"_str));
        auto base        = sys::base_dirs(ResolvedRoots { value.layout, rstd::move(roots) });
        auto base_config = base.config_search_dirs();
        auto base_data   = base.data_search_dirs();
        expect_paths(base_config.as_slice(), { "system"_str });
        expect_paths(base_data.as_slice(), { "system"_str });
        auto project = ProjectDirs::from(base, AppId::parse("Qcm"_str).unwrap());
        EXPECT_TRUE(project.config_dir().is_none());
        EXPECT_TRUE(project.data_dir().is_none());
        expect_paths(project.config_dirs(), { value.config });
        expect_paths(project.data_dirs(), { value.data });
        auto config = project.config_search_dirs();
        auto data   = project.data_search_dirs();
        expect_paths(config.as_slice(), { value.config });
        expect_paths(data.as_slice(), { value.data });
    }
}

TEST(DirsSearch, UserFirstAndStableDeduplication) {
    Roots roots { .config = Some(test_path("user/config"_str)),
                  .data   = Some(test_path("user/data"_str)) };
    for (auto path :
         { "system/config-b"_str, "user/config"_str, "system/config-a"_str, "system/config-b"_str })
        roots.config_dirs.push(test_path(path));
    for (auto path :
         { "system/data-b"_str, "user/data"_str, "system/data-a"_str, "system/data-b"_str })
        roots.data_dirs.push(test_path(path));
    auto base        = sys::base_dirs(ResolvedRoots { Layout::Xdg, rstd::move(roots) });
    auto base_config = base.config_search_dirs();
    auto base_data   = base.data_search_dirs();
    expect_paths(base_config.as_slice(),
                 { "user/config"_str, "system/config-b"_str, "system/config-a"_str });
    expect_paths(base_data.as_slice(),
                 { "user/data"_str, "system/data-b"_str, "system/data-a"_str });

    auto project = ProjectDirs::from(base, AppId::parse("Qcm"_str).unwrap());
    expect_paths(project.config_dirs(),
                 { "system/config-b/Qcm"_str, "user/config/Qcm"_str, "system/config-a/Qcm"_str });
    expect_paths(project.data_dirs(),
                 { "system/data-b/Qcm"_str, "user/data/Qcm"_str, "system/data-a/Qcm"_str });
    auto config = project.config_search_dirs();
    auto data   = project.data_search_dirs();
    expect_paths(config.as_slice(),
                 { "user/config/Qcm"_str, "system/config-b/Qcm"_str, "system/config-a/Qcm"_str });
    expect_paths(data.as_slice(),
                 { "user/data/Qcm"_str, "system/data-b/Qcm"_str, "system/data-a/Qcm"_str });

    base_config[usize()].push(ref<Path>("changed"_str));
    base_data[usize(1)].push(ref<Path>("changed"_str));
    config[usize(1)].push(ref<Path>("changed"_str));
    data.clear();
    auto base_config_again = base.config_search_dirs();
    auto base_data_again   = base.data_search_dirs();
    auto config_again      = project.config_search_dirs();
    auto data_again        = project.data_search_dirs();
    expect_paths(base_config_again.as_slice(),
                 { "user/config"_str, "system/config-b"_str, "system/config-a"_str });
    expect_paths(base_data_again.as_slice(),
                 { "user/data"_str, "system/data-b"_str, "system/data-a"_str });
    expect_paths(config_again.as_slice(),
                 { "user/config/Qcm"_str, "system/config-b/Qcm"_str, "system/config-a/Qcm"_str });
    expect_paths(data_again.as_slice(),
                 { "user/data/Qcm"_str, "system/data-b/Qcm"_str, "system/data-a/Qcm"_str });
}

TEST(DirsSnapshot, BaseOwnsResolvedRootsAndProjectOwnsBaseAndIdResults) {
    auto base = [] {
        auto roots = separate_roots();
        return sys::base_dirs(ResolvedRoots { Layout::Xdg, rstd::move(roots) });
    }();
    expect_separate_base(base);
    auto project = [&] {
        auto id     = AppId::parse("Qcm"_str).unwrap();
        auto result = ProjectDirs::from(base, id);
        id          = AppId::parse("Other"_str).unwrap();
        base        = sys::base_dirs(ResolvedRoots { Layout::Windows, Roots {} });
        expect_path(result.config_dir(), "config/Qcm"_str);
        return result;
    }();
    EXPECT_TRUE(base.config_dir().is_none());
    expect_path(project.config_dir(), "config/Qcm"_str);
    expect_path(project.data_dir(), "data/Qcm"_str);
    expect_path(project.data_local_dir(), "local/Qcm"_str);
    expect_path(project.cache_dir(), "cache/Qcm"_str);
    expect_path(project.state_dir(), "state/Qcm"_str);
    expect_path(project.runtime_dir(), "runtime/Qcm"_str);
    expect_paths(project.config_dirs(), { "system/config-b/Qcm"_str, "system/config-a/Qcm"_str });
    expect_paths(project.data_dirs(), { "system/data-b/Qcm"_str, "system/data-a/Qcm"_str });
}

TEST(DirsLayout, ResolutionAndSearchDoNotCreateDirectories) {
    auto root = rstd::env::temp_dir();
    auto name = rstd::format("rstd-dirs-layout-no-create-{}", rstd::process::id());
    root.push(ref<Path>(name.as_str()));
    auto before = rstd::fs::exists(root.as_path());
    ASSERT_TRUE(before.is_ok());
    ASSERT_FALSE(before.unwrap());

    for (auto layout : { Layout::Xdg, Layout::Windows, Layout::Apple }) {
        Roots roots {
            .config     = Some(root.join(ref<Path>("config"_str))),
            .data       = Some(root.join(ref<Path>("data"_str))),
            .data_local = Some(root.join(ref<Path>("local"_str))),
            .cache      = Some(root.join(ref<Path>("cache"_str))),
            .state      = Some(root.join(ref<Path>("state"_str))),
        };
        roots.config_dirs.push(root.join(ref<Path>("system/config"_str)));
        roots.data_dirs.push(root.join(ref<Path>("system/data"_str)));
        auto base    = sys::base_dirs(ResolvedRoots { layout, rstd::move(roots) });
        auto project = ProjectDirs::from(base, AppId::parse("Qcm"_str).unwrap());
        EXPECT_TRUE(project.config_dir().is_some());
        EXPECT_TRUE(project.data_dir().is_some());
        EXPECT_TRUE(project.data_local_dir().is_some());
        EXPECT_TRUE(project.cache_dir().is_some());
        EXPECT_TRUE(project.state_dir().is_some());
        EXPECT_EQ(base.config_search_dirs().len(), usize(2));
        EXPECT_EQ(base.data_search_dirs().len(), usize(2));
        EXPECT_EQ(project.config_search_dirs().len(), usize(2));
        EXPECT_EQ(project.data_search_dirs().len(), usize(2));
    }
#if RSTD_OS_UNIX
    auto xdg         = resolve(XdgEnvironment {
        .home        = Some(root.clone()),
        .runtime_dir = Some(OsString::from(root.as_path().as_os_str())),
    });
    auto xdg_project = ProjectDirs::from(xdg, AppId::parse("Qcm"_str).unwrap());
    EXPECT_TRUE(xdg_project.config_dir().is_some());
    EXPECT_TRUE(xdg_project.data_dir().is_some());
    EXPECT_TRUE(xdg_project.cache_dir().is_some());
    EXPECT_TRUE(xdg_project.state_dir().is_some());
    EXPECT_TRUE(xdg_project.runtime_dir().is_some());
    EXPECT_EQ(xdg_project.config_search_dirs().len(), usize(2));
    EXPECT_EQ(xdg_project.data_search_dirs().len(), usize(3));
#endif
    auto after = rstd::fs::exists(root.as_path());
    ASSERT_TRUE(after.is_ok());
    EXPECT_FALSE(after.unwrap());
}

#if RSTD_OS_UNIX
TEST(DirsXdg, UsesHomeDefaultsAndOrderedSystemDefaults) {
    auto base = resolve(XdgEnvironment { .home = Some(test_path("home"_str)) });
    expect_home_defaults(base);
    expect_default_system_roots(base);
    auto config = base.config_search_dirs();
    auto data   = base.data_search_dirs();
    ASSERT_EQ(config.len(), usize(2));
    ASSERT_EQ(data.len(), usize(3));
    EXPECT_EQ(config[usize()].as_path(), base.config_dir().unwrap());
    EXPECT_EQ(config[usize(1)].as_path(), ref<Path>("/etc/xdg"_str));
    EXPECT_EQ(data[usize()].as_path(), base.data_dir().unwrap());
    EXPECT_EQ(data[usize(1)].as_path(), ref<Path>("/usr/local/share"_str));
    EXPECT_EQ(data[usize(2)].as_path(), ref<Path>("/usr/share"_str));
}

TEST(DirsXdg, AbsoluteOverridesAreIndependentAndLocalDataMatchesData) {
    auto base = resolve(XdgEnvironment {
        .home        = Some(test_path("home"_str)),
        .config_home = env_path("override/config"_str),
        .data_home   = env_path("override/data"_str),
        .cache_home  = env_path("override/cache"_str),
        .state_home  = env_path("override/state"_str),
        .runtime_dir = env_path("override/runtime"_str),
    });
    expect_path(base.home_dir(), "home"_str);
    expect_path(base.config_dir(), "override/config"_str);
    expect_path(base.data_dir(), "override/data"_str);
    expect_path(base.data_local_dir(), "override/data"_str);
    expect_path(base.cache_dir(), "override/cache"_str);
    expect_path(base.state_dir(), "override/state"_str);
    expect_path(base.runtime_dir(), "override/runtime"_str);
    expect_default_system_roots(base);
}

TEST(DirsXdg, IgnoresEmptyRelativeAndNulContainingOverrides) {
    for (auto value :
         { ""_str, "relative"_str, "./relative"_str, "~/relative"_str, "/absolute\0suffix"_str }) {
        SCOPED_TRACE(value);
        auto base = resolve(XdgEnvironment {
            .home        = Some(test_path("home"_str)),
            .config_home = Some(OsString::from(value)),
            .data_home   = Some(OsString::from(value)),
            .cache_home  = Some(OsString::from(value)),
            .state_home  = Some(OsString::from(value)),
            .runtime_dir = Some(OsString::from(value)),
        });
        expect_home_defaults(base);
        expect_default_system_roots(base);
    }
}

TEST(DirsXdg, MissingOrInvalidHomeKeepsIndependentOverrides) {
    expect_independent_overrides(None());
    expect_independent_overrides(Some(PathBuf::from(""_str)));
    expect_independent_overrides(Some(PathBuf::from("relative/home"_str)));
    expect_independent_overrides(Some(test_path("home\0suffix"_str)));
}

TEST(DirsXdg, DataAndCacheOverridesDoNotRequireHomeOrConfig) {
    auto base = resolve(XdgEnvironment {
        .data_home  = env_path("only/data"_str),
        .cache_home = env_path("only/cache"_str),
    });
    EXPECT_TRUE(base.home_dir().is_none());
    EXPECT_TRUE(base.config_dir().is_none());
    expect_path(base.data_dir(), "only/data"_str);
    expect_path(base.data_local_dir(), "only/data"_str);
    expect_path(base.cache_dir(), "only/cache"_str);
    EXPECT_TRUE(base.state_dir().is_none());
    EXPECT_TRUE(base.runtime_dir().is_none());
    auto data = base.data_search_dirs();
    ASSERT_EQ(data.len(), usize(3));
    EXPECT_EQ(data[usize()].as_path(), base.data_dir().unwrap());
}

TEST(DirsXdg, NoHomeAndNoOverridesRetainsOnlySystemRoots) {
    auto base = resolve(XdgEnvironment {});
    EXPECT_TRUE(base.home_dir().is_none());
    EXPECT_TRUE(base.config_dir().is_none());
    EXPECT_TRUE(base.data_dir().is_none());
    EXPECT_TRUE(base.data_local_dir().is_none());
    EXPECT_TRUE(base.cache_dir().is_none());
    EXPECT_TRUE(base.state_dir().is_none());
    EXPECT_TRUE(base.runtime_dir().is_none());
    expect_default_system_roots(base);
    auto config = base.config_search_dirs();
    auto data   = base.data_search_dirs();
    ASSERT_EQ(config.len(), base.config_dirs().len());
    ASSERT_EQ(data.len(), base.data_dirs().len());
    for (usize index {}; index < config.len(); ++index)
        EXPECT_EQ(config[index].as_path(), base.config_dirs()[index].as_path());
    for (usize index {}; index < data.len(); ++index)
        EXPECT_EQ(data[index].as_path(), base.data_dirs()[index].as_path());
}

TEST(DirsXdg, EmptyOrUnusableListsUseSystemDefaults) {
    for (auto value : { ""_str, ":"_str, "::relative:./other:"_str, "/bad\0entry"_str }) {
        SCOPED_TRACE(value);
        auto base = resolve(XdgEnvironment {
            .config_dirs = Some(OsString::from(value)),
            .data_dirs   = Some(OsString::from(value)),
        });
        expect_default_system_roots(base);
    }
}

TEST(DirsXdg, ListsKeepValidEntriesInOrderAndRemoveDuplicates) {
    auto base = resolve(XdgEnvironment {
        .config_home = env_path("user/config"_str),
        .data_home   = env_path("user/data"_str),
        .config_dirs = Some(OsString::from(
            ":/rstd-dirs-tests/system/config-b:relative:/rstd-dirs-tests/user/config:"
            ":/rstd-dirs-tests/system/config-a:/rstd-dirs-tests/system/config-b/:"
            "/bad\0entry:"_str)),
        .data_dirs =
            Some(OsString::from("/rstd-dirs-tests/system/data-b:/rstd-dirs-tests/user/data:"
                                "./relative::/rstd-dirs-tests/system/data-a:"
                                "/rstd-dirs-tests/system/data-b"_str)),
    });
    expect_paths(base.config_dirs(),
                 { "system/config-b"_str, "user/config"_str, "system/config-a"_str });
    expect_paths(base.data_dirs(), { "system/data-b"_str, "user/data"_str, "system/data-a"_str });
    auto config = base.config_search_dirs();
    auto data   = base.data_search_dirs();
    expect_paths(config.as_slice(),
                 { "user/config"_str, "system/config-b"_str, "system/config-a"_str });
    expect_paths(data.as_slice(), { "user/data"_str, "system/data-b"_str, "system/data-a"_str });
}

TEST(DirsXdg, PreservesNativeBytesInHomeOverridesListsAndProjectPaths) {
    auto home    = ref<OsStr>::from_encoded_bytes_unchecked("/rstd-dirs-tests/home/\xff"_bytes);
    auto config  = ref<OsStr>::from_encoded_bytes_unchecked("/rstd-dirs-tests/config/\xfe"_bytes);
    auto systems = ref<OsStr>::from_encoded_bytes_unchecked(
        "/rstd-dirs-tests/system/\xfd:/rstd-dirs-tests/system/\xfc:"
        "/rstd-dirs-tests/system/\xfd"_bytes);
    auto base = resolve(XdgEnvironment {
        .home        = Some(PathBuf::from(OsString::from(home))),
        .config_home = Some(OsString::from(config)),
        .config_dirs = Some(OsString::from(systems)),
        .data_dirs   = Some(OsString::from(systems)),
    });
    ASSERT_TRUE(base.home_dir().is_some());
    ASSERT_TRUE(base.config_dir().is_some());
    ASSERT_TRUE(base.data_dir().is_some());
    EXPECT_TRUE(base.home_dir()->to_str().is_none());
    EXPECT_EQ(base.home_dir()->as_os_str().as_encoded_bytes(), home.as_encoded_bytes());
    EXPECT_EQ(base.config_dir()->as_os_str().as_encoded_bytes(), config.as_encoded_bytes());
    EXPECT_EQ(base.data_dir()->as_os_str().as_encoded_bytes(),
              "/rstd-dirs-tests/home/\xff/.local/share"_bytes);
    ASSERT_EQ(base.config_dirs().len(), usize(2));
    ASSERT_EQ(base.data_dirs().len(), usize(2));
    EXPECT_EQ(base.config_dirs()[usize()].as_path().as_os_str().as_encoded_bytes(),
              "/rstd-dirs-tests/system/\xfd"_bytes);
    EXPECT_EQ(base.data_dirs()[usize(1)].as_path().as_os_str().as_encoded_bytes(),
              "/rstd-dirs-tests/system/\xfc"_bytes);

    auto project = ProjectDirs::from(base, AppId::parse("Qcm"_str).unwrap());
    ASSERT_TRUE(project.config_dir().is_some());
    ASSERT_TRUE(project.data_dir().is_some());
    EXPECT_EQ(project.config_dir()->as_os_str().as_encoded_bytes(),
              "/rstd-dirs-tests/config/\xfe/Qcm"_bytes);
    EXPECT_EQ(project.data_dir()->as_os_str().as_encoded_bytes(),
              "/rstd-dirs-tests/home/\xff/.local/share/Qcm"_bytes);
    auto search = project.data_search_dirs();
    ASSERT_EQ(search.len(), usize(3));
    EXPECT_EQ(search[usize(1)].as_path().as_os_str().as_encoded_bytes(),
              "/rstd-dirs-tests/system/\xfd/Qcm"_bytes);
    EXPECT_EQ(search[usize(2)].as_path().as_os_str().as_encoded_bytes(),
              "/rstd-dirs-tests/system/\xfc/Qcm"_bytes);
}

TEST(DirsSnapshot, CapturedEnvironmentAndLaterResolutionDoNotChangeExistingObjects) {
    auto home       = test_path("home"_str);
    auto configured = OsString::from("/rstd-dirs-tests/captured/config"_str);
    auto base       = resolve(XdgEnvironment {
        .home        = Some(home.clone()),
        .config_home = Some(configured.clone()),
    });
    home.push(ref<Path>("changed"_str));
    configured.clear();
    configured.push("/rstd-dirs-tests/later/config"_str);
    auto later = resolve(XdgEnvironment {
        .home        = Some(rstd::move(home)),
        .config_home = Some(rstd::move(configured)),
    });
    expect_path(base.home_dir(), "home"_str);
    expect_path(base.config_dir(), "captured/config"_str);
    expect_path(later.home_dir(), "home/changed"_str);
    expect_path(later.config_dir(), "later/config"_str);
    auto project = ProjectDirs::from(base, AppId::parse("Qcm"_str).unwrap());
    expect_path(project.config_dir(), "captured/config/Qcm"_str);
    expect_path(project.data_dir(), "home/.local/share/Qcm"_str);
}
#endif
