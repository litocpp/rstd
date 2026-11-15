#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>

import rstd;

#include "support.hpp"

TEST(DirsAppId, PreservesPortableComponents) {
    const ref<str> values[] = {
        "Qcm"_str,
        "qcm-player_2.0"_str,
        "ABCxyz019._-end"_str,
        "-"_str,
        "_"_str,
        ".hidden"_str,
        "a..b"_str,
        "com0"_str,
        "COM10"_str,
        "lpt0"_str,
        "LPT10.log"_str,
        "console"_str,
        "auxiliary"_str,
        "con-file"_str,
        "con_file"_str,
        "com1suffix"_str,
        "prefix.CON"_str,
    };
    for (auto value : values) {
        SCOPED_TRACE(value);
        auto parsed = AppId::parse(value);
        ASSERT_TRUE(parsed.is_ok());
        auto id = parsed.unwrap();
        EXPECT_EQ(id.as_str(), value);
    }
}

TEST(DirsAppId, RejectsInvalidComponents) {
    const ref<str> values[] = {
        ""_str,          "."_str,       ".."_str,        "..."_str,
        "Qcm."_str,      "Qcm.."_str,   "a/b"_str,       "a\\b"_str,
        "/absolute"_str, "C:drive"_str, "two words"_str, " trailing"_str,
        "trailing "_str, "a\t"_str,     "a\n"_str,       "a\0b"_str,
        "a\x7f"_str,     "a:b"_str,     "a*b"_str,       "a?b"_str,
        "a\"b"_str,      "a<b"_str,     "a>b"_str,       "a|b"_str,
        "a+b"_str,       "a@b"_str,     "a\xc3\xa9"_str, "a\xef\xbc\x8f"_str,
    };
    for (auto value : values) {
        SCOPED_TRACE(value);
        auto result = AppId::parse(value);
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err().kind(), Error::Tag::InvalidAppId);
    }
}

TEST(DirsAppId, RejectsWindowsDeviceNamesRegardlessOfCaseOrExtension) {
    const ref<str> names[] = {
        "CON"_str,  "con"_str,  "CoN"_str,  "PRN"_str,  "pRn"_str,  "AUX"_str,
        "AuX"_str,  "NUL"_str,  "nUl"_str,  "COM1"_str, "COM2"_str, "COM3"_str,
        "COM4"_str, "COM5"_str, "COM6"_str, "COM7"_str, "COM8"_str, "COM9"_str,
        "LPT1"_str, "LPT2"_str, "LPT3"_str, "LPT4"_str, "LPT5"_str, "LPT6"_str,
        "LPT7"_str, "LPT8"_str, "LPT9"_str, "cOm1"_str, "lPt9"_str,
    };
    for (auto name : names) {
        SCOPED_TRACE(name);
        auto result = AppId::parse(name);
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err().kind(), Error::Tag::InvalidAppId);
        for (auto extension : { ".txt"_str, ".settings.json"_str }) {
            auto extended = rstd::format("{}{}", name, extension);
            auto rejected = AppId::parse(extended.as_str());
            ASSERT_TRUE(rejected.is_err());
            EXPECT_EQ(rejected.unwrap_err().kind(), Error::Tag::InvalidAppId);
        }
    }
}

TEST(DirsAppId, OwnsParsedInput) {
    auto parsed = [] {
        auto input = String::make("Mixed.Case-ID_42"_str);
        auto value = AppId::parse(input.as_str());
        input.clear();
        input.push_str("replacement"_str);
        return value;
    }();
    ASSERT_TRUE(parsed.is_ok());
    auto id = parsed.unwrap();
    EXPECT_EQ(id.as_str(), "Mixed.Case-ID_42"_str);
}

TEST(DirsAppRoots, AcceptsEmptyAndPartialSnapshotsWithoutFallbacks) {
    auto empty = ProjectDirs::from_roots(AppRoots {});
    ASSERT_TRUE(empty.is_ok());
    expect_missing(empty.unwrap());

    auto partial = ProjectDirs::from_roots(AppRoots { .runtime = Some(test_path("runtime"_str)) });
    ASSERT_TRUE(partial.is_ok());
    auto dirs = partial.unwrap();
    expect_path(dirs.runtime_dir(), "runtime"_str);
    EXPECT_TRUE(dirs.config_dir().is_none());
    EXPECT_TRUE(dirs.data_dir().is_none());
    EXPECT_TRUE(dirs.data_local_dir().is_none());
    EXPECT_TRUE(dirs.cache_dir().is_none());
    EXPECT_TRUE(dirs.state_dir().is_none());
    EXPECT_TRUE(dirs.config_search_dirs().is_empty());
    EXPECT_TRUE(dirs.data_search_dirs().is_empty());
}

TEST(DirsAppRoots, KeepsAlreadyQualifiedDirectories) {
    auto result = ProjectDirs::from_roots(AppRoots {
        .config     = Some(test_path("host/app/config"_str)),
        .data       = Some(test_path("host/app/data"_str)),
        .data_local = Some(test_path("host/app/local"_str)),
        .cache      = Some(test_path("host/app/cache"_str)),
        .state      = Some(test_path("host/app/state"_str)),
        .runtime    = Some(test_path("host/app/runtime"_str)),
    });
    ASSERT_TRUE(result.is_ok());
    auto dirs = result.unwrap();
    expect_path(dirs.config_dir(), "host/app/config"_str);
    expect_path(dirs.data_dir(), "host/app/data"_str);
    expect_path(dirs.data_local_dir(), "host/app/local"_str);
    expect_path(dirs.cache_dir(), "host/app/cache"_str);
    expect_path(dirs.state_dir(), "host/app/state"_str);
    expect_path(dirs.runtime_dir(), "host/app/runtime"_str);
    EXPECT_TRUE(dirs.config_dirs().is_empty());
    EXPECT_TRUE(dirs.data_dirs().is_empty());
    auto config = dirs.config_search_dirs();
    auto data   = dirs.data_search_dirs();
    expect_paths(config.as_slice(), { "host/app/config"_str });
    expect_paths(data.as_slice(), { "host/app/data"_str });
}

TEST(DirsAppRoots, AllowsSemanticDirectoriesToShareOnePath) {
    auto result = ProjectDirs::from_roots(AppRoots {
        .config     = Some(test_path("shared"_str)),
        .data       = Some(test_path("shared"_str)),
        .data_local = Some(test_path("shared"_str)),
        .cache      = Some(test_path("shared"_str)),
        .state      = Some(test_path("shared"_str)),
        .runtime    = Some(test_path("shared"_str)),
    });
    ASSERT_TRUE(result.is_ok());
    auto dirs = result.unwrap();
    expect_path(dirs.config_dir(), "shared"_str);
    expect_path(dirs.data_dir(), "shared"_str);
    expect_path(dirs.data_local_dir(), "shared"_str);
    expect_path(dirs.cache_dir(), "shared"_str);
    expect_path(dirs.state_dir(), "shared"_str);
    expect_path(dirs.runtime_dir(), "shared"_str);
}

TEST(DirsAppRoots, ValidatesEverySuppliedDirectory) {
    Option<PathBuf> AppRoots::* const fields[] = {
        &AppRoots::config, &AppRoots::data,  &AppRoots::data_local,
        &AppRoots::cache,  &AppRoots::state, &AppRoots::runtime,
    };
    const ref<str> invalid[] = { ""_str, "relative"_str, "relative/path"_str, "."_str, ".."_str };
    for (auto field : fields) {
        for (auto path : invalid) {
            SCOPED_TRACE(path);
            AppRoots roots {};
            roots.*field = Some(PathBuf::from(path));
            auto result  = ProjectDirs::from_roots(rstd::move(roots));
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err().kind(), Error::Tag::InvalidPath);
        }
        AppRoots roots {};
        roots.*field = Some(test_path("absolute\0suffix"_str));
        auto result  = ProjectDirs::from_roots(rstd::move(roots));
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err().kind(), Error::Tag::InvalidPath);
#if RSTD_OS_WINDOWS
        for (auto path : { "C:relative"_str, "\\rooted-without-drive"_str }) {
            AppRoots rooted {};
            rooted.*field = Some(PathBuf::from(path));
            auto rejected = ProjectDirs::from_roots(rstd::move(rooted));
            ASSERT_TRUE(rejected.is_err());
            EXPECT_EQ(rejected.unwrap_err().kind(), Error::Tag::InvalidPath);
        }
#endif
    }
}

TEST(DirsAppRoots, OwnsInputsAndReturnedSearchCollections) {
    auto result = [] {
        AppRoots roots { .config = Some(test_path("owned/config"_str)),
                         .data   = Some(test_path("owned/data"_str)) };
        return ProjectDirs::from_roots(rstd::move(roots));
    }();
    ASSERT_TRUE(result.is_ok());
    auto dirs   = result.unwrap();
    auto config = dirs.config_search_dirs();
    auto data   = dirs.data_search_dirs();
    config[usize()].push(ref<Path>("changed"_str));
    data.clear();
    expect_path(dirs.config_dir(), "owned/config"_str);
    expect_path(dirs.data_dir(), "owned/data"_str);
    auto config_again = dirs.config_search_dirs();
    auto data_again   = dirs.data_search_dirs();
    expect_paths(config_again.as_slice(), { "owned/config"_str });
    expect_paths(data_again.as_slice(), { "owned/data"_str });
}

TEST(DirsAppRoots, DoesNotNormalizeOrRequireExistingDirectories) {
    auto expected = test_path("absent/./nested/../app"_str);
    auto result   = ProjectDirs::from_roots(AppRoots { .config = Some(expected.clone()) });
    ASSERT_TRUE(result.is_ok());
    auto dirs   = result.unwrap();
    auto actual = dirs.config_dir();
    ASSERT_TRUE(actual.is_some());
    EXPECT_EQ(actual->as_os_str().as_encoded_bytes(),
              expected.as_path().as_os_str().as_encoded_bytes());
}

TEST(DirsAppRoots, ResolutionDoesNotCreateDirectories) {
    auto root = rstd::env::temp_dir();
    auto name = rstd::format("rstd-dirs-no-create-{}", rstd::process::id());
    root.push(ref<Path>(name.as_str()));
    auto before = rstd::fs::exists(root.as_path());
    ASSERT_TRUE(before.is_ok());
    ASSERT_FALSE(before.unwrap());

    auto config = root.join(ref<Path>("config"_str));
    auto data   = root.join(ref<Path>("data"_str));
    auto result = ProjectDirs::from_roots(AppRoots {
        .config     = Some(config.clone()),
        .data       = Some(data.clone()),
        .data_local = Some(root.join(ref<Path>("local"_str))),
        .cache      = Some(root.join(ref<Path>("cache"_str))),
        .state      = Some(root.join(ref<Path>("state"_str))),
        .runtime    = Some(root.join(ref<Path>("runtime"_str))),
    });
    ASSERT_TRUE(result.is_ok());
    auto dirs = result.unwrap();
    expect_path(dirs.config_dir(), config);
    expect_path(dirs.data_dir(), data);
    EXPECT_TRUE(dirs.data_local_dir().is_some());
    EXPECT_TRUE(dirs.cache_dir().is_some());
    EXPECT_TRUE(dirs.state_dir().is_some());
    EXPECT_TRUE(dirs.runtime_dir().is_some());
    EXPECT_EQ(dirs.config_search_dirs().len(), usize(1));
    EXPECT_EQ(dirs.data_search_dirs().len(), usize(1));
    auto after = rstd::fs::exists(root.as_path());
    ASSERT_TRUE(after.is_ok());
    EXPECT_FALSE(after.unwrap());
}

#if RSTD_OS_UNIX
TEST(DirsAppRoots, PreservesNativeBytes) {
    auto bytes  = ref<OsStr>::from_encoded_bytes_unchecked("/rstd-dirs-tests/host/\xff"_bytes);
    auto result = ProjectDirs::from_roots(AppRoots {
        .data = Some(PathBuf::from(OsString::from(bytes))),
    });
    ASSERT_TRUE(result.is_ok());
    auto dirs   = result.unwrap();
    auto actual = dirs.data_dir();
    ASSERT_TRUE(actual.is_some());
    EXPECT_TRUE(actual->to_str().is_none());
    EXPECT_EQ(actual->as_os_str().as_encoded_bytes(), bytes.as_encoded_bytes());
    auto search = dirs.data_search_dirs();
    ASSERT_EQ(search.len(), usize(1));
    EXPECT_EQ(search[usize()].as_path().as_os_str().as_encoded_bytes(), bytes.as_encoded_bytes());
}
#endif

#if RSTD_OS_WINDOWS
TEST(DirsAppRoots, PreservesNativeWidePaths) {
    const rstd::u16 units[] = { rstd::u16(0xd800), rstd::u16('x') };
    auto            suffix  = rstd::os::windows::ffi::OsStringExt::from_wide(
        slice<rstd::u16>::from_raw_parts(units, usize(2)));
    auto native = OsString::from("C:\\rstd-dirs-tests\\"_str);
    native.push(suffix.as_os_str());
    auto expected = native.clone();
    auto result   = ProjectDirs::from_roots(AppRoots {
        .data = Some(PathBuf::from(rstd::move(native))),
    });
    ASSERT_TRUE(result.is_ok());
    auto dirs   = result.unwrap();
    auto actual = dirs.data_dir();
    ASSERT_TRUE(actual.is_some());
    EXPECT_TRUE(actual->to_str().is_none());
    EXPECT_EQ(actual->as_os_str().as_encoded_bytes(), expected.as_os_str().as_encoded_bytes());
    auto search = dirs.data_search_dirs();
    ASSERT_EQ(search.len(), usize(1));
    EXPECT_EQ(search[usize()].as_path().as_os_str().as_encoded_bytes(),
              expected.as_os_str().as_encoded_bytes());
}
#endif
