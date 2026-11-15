#ifndef RSTD_DIRS_TEST_SUPPORT_HPP
#define RSTD_DIRS_TEST_SUPPORT_HPP

namespace
{
using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::dirs::AppId;
using rstd::dirs::AppRoots;
using rstd::dirs::BaseDirs;
using rstd::dirs::Error;
using rstd::dirs::ProjectDirs;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;
using rstd::path::Path;
using rstd::path::PathBuf;

auto test_path(ref<str> suffix) -> PathBuf {
#if RSTD_OS_WINDOWS
    auto path = PathBuf::from("C:/rstd-dirs-tests"_str);
#else
    auto path = PathBuf::from("/rstd-dirs-tests"_str);
#endif
    path.push(ref<Path>(suffix));
    return path;
}

auto expect_path(Option<ref<Path>> actual, const PathBuf& expected) -> void {
    ASSERT_TRUE(actual.is_some());
    EXPECT_EQ(*actual, expected.as_path());
}

auto expect_path(Option<ref<Path>> actual, ref<str> suffix) -> void {
    expect_path(actual, test_path(suffix));
}

template<unsigned long Size>
auto expect_paths(slice<PathBuf> actual, const ref<str> (&suffixes)[Size]) -> void {
    ASSERT_EQ(actual.len(), usize(Size));
    for (usize index {}; index < usize(Size); ++index) {
        auto expected = test_path(suffixes[index.to_primitive()]);
        EXPECT_EQ(actual[index].as_path(), expected.as_path()) << index.to_primitive();
    }
}

auto expect_missing(const ProjectDirs& dirs) -> void {
    EXPECT_TRUE(dirs.config_dir().is_none());
    EXPECT_TRUE(dirs.data_dir().is_none());
    EXPECT_TRUE(dirs.data_local_dir().is_none());
    EXPECT_TRUE(dirs.cache_dir().is_none());
    EXPECT_TRUE(dirs.state_dir().is_none());
    EXPECT_TRUE(dirs.runtime_dir().is_none());
    EXPECT_TRUE(dirs.config_dirs().is_empty());
    EXPECT_TRUE(dirs.data_dirs().is_empty());
    EXPECT_TRUE(dirs.config_search_dirs().is_empty());
    EXPECT_TRUE(dirs.data_search_dirs().is_empty());
}
} // namespace

#endif
