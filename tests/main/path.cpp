#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>
import rstd;

TEST(Path, PublicSeparatorsPreserveUnicode) {
    EXPECT_TRUE(rstd::path::is_separator(U'/'));
    EXPECT_FALSE(rstd::path::is_separator(U'界'));
    EXPECT_FALSE(rstd::path::is_separator(U'／'));
    EXPECT_TRUE(rstd::path::is_separator(rstd::path::MAIN_SEPARATOR));
}

using rstd::path::Component;
using rstd::path::Path;
using rstd::path::PathBuf;
using namespace rstd::literals;

// ── ref<Path> ────────────────────────────────────────────────────────────

TEST(Path, IsAbsolute) {
#if RSTD_OS_WINDOWS
    EXPECT_TRUE(rstd::ref<Path>(R"(C:\usr\bin)"_str).is_absolute());
    EXPECT_TRUE(rstd::ref<Path>(R"(\\server\share)"_str).is_absolute());
    EXPECT_FALSE(rstd::ref<Path>("/usr/bin"_str).is_absolute());
#else
    EXPECT_TRUE(rstd::ref<Path>("/usr/bin"_str).is_absolute());
#endif
    EXPECT_FALSE(rstd::ref<Path>("relative/path"_str).is_absolute());
    EXPECT_FALSE(rstd::ref<Path>(""_str).is_absolute());
}

TEST(Path, SetExtension) {
    struct Case {
        rstd::ref<rstd::str> input, extension, expected;
        bool                 changed;
    };
    const Case cases[] = {
        { "scene.json"_str, "pkg"_str, "scene.pkg"_str, true },
        { "archive.tar.gz"_str, ""_str, "archive.tar"_str, true },
        { ".hidden"_str, "pkg"_str, ".hidden.pkg"_str, true },
        { "file."_str, "json"_str, "file.json"_str, true },
        { "dir.name/file"_str, "json"_str, "dir.name/file.json"_str, true },
        { "dir/file/"_str, "pkg"_str, "dir/file.pkg"_str, true },
        { ""_str, "pkg"_str, ""_str, false },
        { "/"_str, "pkg"_str, "/"_str, false },
        { "."_str, "pkg"_str, "."_str, false },
        { "a/.."_str, "pkg"_str, "a/.."_str, false },
        { "dir/file/."_str, "pkg"_str, "dir/file.pkg"_str, true },
        { "a.txt"_str, "tar.gz"_str, "a.tar.gz"_str, true },
        { ".."_str, "pkg"_str, ".."_str, false },
    };
    for (const auto& value : cases) {
        auto path = PathBuf::from(value.input);
        EXPECT_EQ(path.set_extension(rstd::ref<rstd::ffi::OsStr>(value.extension)), value.changed);
        EXPECT_EQ(path.as_path().to_str().unwrap(), value.expected);
    }
    auto alias = PathBuf::from("name.pkg"_str);
    EXPECT_TRUE(alias.set_extension(*alias.as_path().extension()));
    EXPECT_EQ(alias.as_path().to_str().unwrap(), "name.pkg"_str);
    auto whole = PathBuf::from("name.pkg"_str);
    EXPECT_TRUE(whole.set_extension(*whole.as_path().file_name()));
    EXPECT_EQ(whole.as_path().to_str().unwrap(), "name.name.pkg"_str);
}

TEST(Path, FileStem) {
    EXPECT_EQ(rstd::ref<Path>("file.tar.gz"_str).file_stem()->to_str().unwrap(), "file.tar"_str);
    EXPECT_EQ(rstd::ref<Path>(".hidden"_str).file_stem()->to_str().unwrap(), ".hidden"_str);
    EXPECT_EQ(rstd::ref<Path>("file."_str).file_stem()->to_str().unwrap(), "file"_str);
    EXPECT_EQ(rstd::ref<Path>("dir/file/."_str).file_name()->to_str().unwrap(), "file"_str);
    EXPECT_EQ(rstd::ref<Path>("dir/file/."_str).file_stem()->to_str().unwrap(), "file"_str);
    EXPECT_TRUE(rstd::ref<Path>("."_str).file_stem().is_none());
    EXPECT_TRUE(rstd::ref<Path>(".."_str).file_name().is_none());
    EXPECT_TRUE(rstd::ref<Path>(".."_str).extension().is_none());
    EXPECT_TRUE(rstd::ref<Path>("/"_str).file_stem().is_none());
}

TEST(Path, SetExtensionRejectsSeparators) {
    auto path = PathBuf::from("name.txt"_str);
    EXPECT_DEATH(path.set_extension(rstd::ref<rstd::ffi::OsStr>("a/b"_str)),
                 "extension cannot contain path separators");
    auto empty = PathBuf::make();
    EXPECT_DEATH(empty.set_extension(rstd::ref<rstd::ffi::OsStr>("/"_str)),
                 "extension cannot contain path separators");
#if RSTD_OS_WINDOWS
    EXPECT_DEATH(path.set_extension(rstd::ref<rstd::ffi::OsStr>("a\\b"_str)),
                 "extension cannot contain path separators");
#endif
}

TEST(Path, SetExtensionReusesAllocation) {
    auto path     = PathBuf::from("a.very_long_extension"_str);
    auto before   = path.as_path().as_os_str();
    auto address  = before.as_encoded_bytes().as_raw_ptr();
    auto capacity = path.capacity();
    EXPECT_TRUE(path.set_extension(rstd::ref<rstd::ffi::OsStr>("pkg"_str)));
    EXPECT_EQ(path.as_path().as_os_str().as_encoded_bytes().as_raw_ptr(), address);
    EXPECT_EQ(path.capacity(), capacity);
    EXPECT_EQ(path.as_path().to_str().unwrap(), "a.pkg"_str);
}

TEST(Path, IsSafeRelative) {
    EXPECT_TRUE(rstd::ref<Path>("sources/archive"_str).is_safe_relative());
    EXPECT_TRUE(rstd::ref<Path>("./sources/archive"_str).is_safe_relative());
    EXPECT_TRUE(rstd::ref<Path>(""_str).is_safe_relative());
    EXPECT_FALSE(rstd::ref<Path>("../archive"_str).is_safe_relative());
    EXPECT_FALSE(rstd::ref<Path>("sources/../../archive"_str).is_safe_relative());
    EXPECT_FALSE(rstd::ref<Path>("/sources/archive"_str).is_safe_relative());
}

TEST(Path, Parent) {
    auto p = rstd::ref<Path>("/usr/bin/ls"_str).parent();
    ASSERT_TRUE(p.is_some());
    auto parent = (*p).to_str();
    ASSERT_TRUE(parent.is_some());
    EXPECT_EQ(*parent, "/usr/bin"_str);
}

TEST(Path, ParentRoot) {
    // "/" has no parent (it IS the root)
    auto p = rstd::ref<Path>("/"_str).parent();
    EXPECT_TRUE(p.is_none());
}

TEST(Path, ParentRelative) {
    auto p = rstd::ref<Path>("foo/bar"_str).parent();
    ASSERT_TRUE(p.is_some());
    EXPECT_EQ((*p).to_str().unwrap(), "foo"_str);
}

TEST(Path, ParentNoSep) {
    auto parent = rstd::ref<Path>("file.txt"_str).parent();
    ASSERT_TRUE(parent.is_some());
    EXPECT_TRUE(parent->is_empty());
}

TEST(Path, PopComponents) {
    struct Case {
        rstd::ref<rstd::str> input, expected;
        bool                 removed;
    };
    const Case cases[] = {
        { ""_str, ""_str, false },      { "/"_str, "/"_str, false },
        { "a"_str, ""_str, true },      { "a/"_str, ""_str, true },
        { "."_str, ""_str, true },      { ".."_str, ""_str, true },
        { "a/."_str, ""_str, true },    { "a/b/./"_str, "a"_str, true },
        { "a/./b"_str, "a"_str, true }, { "a/../b"_str, "a/.."_str, true },
        { "./a"_str, "."_str, true },   { "/a"_str, "/"_str, true },
        { "/."_str, "/."_str, false },  { "a/.."_str, "a"_str, true },
    };
    for (const auto& value : cases) {
        auto path     = PathBuf::from(value.input);
        auto capacity = path.capacity();
        EXPECT_EQ(path.pop(), value.removed);
        EXPECT_EQ(path.as_path().to_str().unwrap(), value.expected);
        EXPECT_EQ(path.capacity(), capacity);
    }
    auto path = PathBuf::from("a/b"_str);
    EXPECT_TRUE(path.pop());
    EXPECT_TRUE(path.pop());
    EXPECT_FALSE(path.pop());
    EXPECT_TRUE(path.is_empty());
}

TEST(Path, FileName) {
    auto f = rstd::ref<Path>("/usr/bin/ls"_str).file_name();
    ASSERT_TRUE(f.is_some());
    EXPECT_EQ((*f).to_str().unwrap(), "ls"_str);
}

TEST(Path, FileNameDir) {
    auto f = rstd::ref<Path>("/usr/bin/"_str).file_name();
    ASSERT_TRUE(f.is_some());
    EXPECT_EQ((*f).to_str().unwrap(), "bin"_str);
}

TEST(Path, Extension) {
    auto e = rstd::ref<Path>("file.tar.gz"_str).extension();
    ASSERT_TRUE(e.is_some());
    EXPECT_EQ((*e).to_str().unwrap(), "gz"_str);
}

TEST(Path, ExtensionNone) {
    EXPECT_TRUE(rstd::ref<Path>("Makefile"_str).extension().is_none());
}

TEST(Path, ExtensionDotFile) {
    // ".gitignore" — the leading dot is not an extension separator
    EXPECT_TRUE(rstd::ref<Path>(".gitignore"_str).extension().is_none());
}

TEST(Path, ToStr) {
    auto s = rstd::ref<Path>("/tmp/test"_str).to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(*s, "/tmp/test"_str);
}

TEST(Path, ComponentsAbsolute) {
    auto c = rstd::ref<Path>("/tmp/foo.txt"_str).components();
    EXPECT_EQ(c.next().unwrap(), Component::root_dir());
    auto tmp = c.next();
    ASSERT_TRUE(tmp.is_some());
    EXPECT_TRUE((*tmp).is_normal());
    EXPECT_EQ((*tmp).as_os_str().to_str().unwrap(), "tmp"_str);
    auto file = c.next();
    ASSERT_TRUE(file.is_some());
    EXPECT_TRUE((*file).is_normal());
    EXPECT_EQ((*file).as_os_str().to_str().unwrap(), "foo.txt"_str);
    EXPECT_TRUE(c.next().is_none());
}

TEST(Path, ComponentsNormaliseSeparatorsAndDot) {
    auto c = rstd::ref<Path>("a//./b/."_str).components();
    auto a = c.next();
    ASSERT_TRUE(a.is_some());
    EXPECT_EQ((*a).as_os_str().to_str().unwrap(), "a"_str);
    auto b = c.next();
    ASSERT_TRUE(b.is_some());
    EXPECT_EQ((*b).as_os_str().to_str().unwrap(), "b"_str);
    EXPECT_TRUE(c.next().is_none());
}

TEST(Path, ComponentsLeadingCurDir) {
    auto c = rstd::ref<Path>("./a/.."_str).components();
    EXPECT_EQ(c.next().unwrap(), Component::cur_dir());
    auto a = c.next();
    ASSERT_TRUE(a.is_some());
    EXPECT_EQ((*a).as_os_str().to_str().unwrap(), "a"_str);
    EXPECT_EQ(c.next().unwrap(), Component::parent_dir());
    EXPECT_TRUE(c.next().is_none());
}

TEST(Path, ComponentsSupportsRangeFor) {
    auto components = rstd::ref<Path>("/tmp/example"_str).components();
    auto count      = rstd::usize {};
    for (auto component : components) {
        EXPECT_FALSE(component.is_cur_dir());
        ++count;
    }
    EXPECT_EQ(count, rstd::usize(3));
}

TEST(Path, ValueEqualityUsesComponents) {
    auto left  = PathBuf::from("a//./b/"_str);
    auto right = PathBuf::from("a/b"_str);
    EXPECT_TRUE(left.as_path() == right.as_path());
    EXPECT_FALSE(left.as_path() == rstd::ref<Path>("a/c"_str));
    EXPECT_FALSE(rstd::ref<Path>("a/../b"_str) == rstd::ref<Path>("b"_str));
    EXPECT_TRUE(rstd::ref<Path>(""_str) == rstd::ref<Path>(""_str));
}

TEST(Path, ValueEqualitySupportsNonUtf8) {
    auto left =
        rstd::ref<Path>(rstd::ref<rstd::ffi::OsStr>::from_encoded_bytes_unchecked("a/\xff"_bytes));
    auto same =
        rstd::ref<Path>(rstd::ref<rstd::ffi::OsStr>::from_encoded_bytes_unchecked("a/\xff"_bytes));
    auto different =
        rstd::ref<Path>(rstd::ref<rstd::ffi::OsStr>::from_encoded_bytes_unchecked("a/\xfe"_bytes));
    EXPECT_TRUE(left == same);
    EXPECT_FALSE(left == different);
}

TEST(Path, HasRoot) {
    EXPECT_TRUE(rstd::ref<Path>("/usr"_str).has_root());
    EXPECT_FALSE(rstd::ref<Path>("usr"_str).has_root());
}

TEST(Path, StartsWithComponentBoundary) {
    EXPECT_TRUE(
        rstd::ref<Path>("/assets/scene.json"_str).starts_with(rstd::ref<Path>("/assets"_str)));
    EXPECT_TRUE(
        rstd::ref<Path>("/assets/scene.json"_str).starts_with(rstd::ref<Path>("/assets/"_str)));
    EXPECT_FALSE(
        rstd::ref<Path>("/assets/scene.json"_str).starts_with(rstd::ref<Path>("/asset"_str)));
    EXPECT_FALSE(
        rstd::ref<Path>("/assets/scene.json"_str).starts_with(rstd::ref<Path>("assets"_str)));
}

TEST(Path, StripPrefix) {
    auto stripped =
        rstd::ref<Path>("/assets/scene.json"_str).strip_prefix(rstd::ref<Path>("/assets"_str));
    ASSERT_TRUE(stripped.is_some());
    EXPECT_EQ((*stripped).to_str().unwrap(), "scene.json"_str);
}

TEST(Path, StripPrefixRootAndExact) {
    auto root = rstd::ref<Path>("/test/haha/foo.txt"_str).strip_prefix(rstd::ref<Path>("/"_str));
    ASSERT_TRUE(root.is_some());
    EXPECT_EQ((*root).to_str().unwrap(), "test/haha/foo.txt"_str);

    auto exact = rstd::ref<Path>("/test/haha/foo.txt/"_str)
                     .strip_prefix(rstd::ref<Path>("/test/haha/foo.txt"_str));
    ASSERT_TRUE(exact.is_some());
    EXPECT_TRUE((*exact).is_empty());
}

TEST(Path, StripPrefixMismatch) {
    EXPECT_TRUE(rstd::ref<Path>("/assets/scene.json"_str)
                    .strip_prefix(rstd::ref<Path>("/asset"_str))
                    .is_none());
    EXPECT_TRUE(rstd::ref<Path>("/assets/scene.json"_str)
                    .strip_prefix(rstd::ref<Path>("assets"_str))
                    .is_none());
}

// ── PathBuf ──────────────────────────────────────────────────────────────

TEST(PathBuf, MakeEmpty) {
    auto p = PathBuf::make();
    EXPECT_TRUE(p.is_empty());
    EXPECT_EQ(p.capacity(), rstd::usize {});
}

static_assert(rstd::Impled<PathBuf, rstd::clone::Clone>);

TEST(PathBuf, CloneOwnsIndependentStorage) {
#if RSTD_OS_WINDOWS
    auto original = PathBuf::from(R"(C:\tmp\original)"_str);
#else
    auto original = PathBuf::from("/tmp/original"_str);
#endif
    auto cloned = original.clone();
    cloned.push(rstd::ref<Path>("child"_str));

#if RSTD_OS_WINDOWS
    EXPECT_EQ(original.as_path().to_str().unwrap(), R"(C:\tmp\original)"_str);
    EXPECT_EQ(cloned.as_path().to_str().unwrap(), R"(C:\tmp\original\child)"_str);
#else
    EXPECT_EQ(original.as_path().to_str().unwrap(), "/tmp/original"_str);
    EXPECT_EQ(cloned.as_path().to_str().unwrap(), "/tmp/original/child"_str);
#endif
}

TEST(PathBuf, FromStr) {
#if RSTD_OS_WINDOWS
    auto p = PathBuf::from(R"(C:\usr\bin)"_str);
    EXPECT_EQ(p.len(), rstd::usize(10));
#else
    auto p = PathBuf::from("/usr/bin"_str);
    EXPECT_EQ(p.len(), rstd::usize(8));
#endif
    EXPECT_TRUE(p.as_path().is_absolute());
    EXPECT_GE(p.capacity(), p.len());
}

TEST(PathBuf, FromPath) {
    auto p = PathBuf::from(rstd::ref<Path>("/usr/bin"_str));
    EXPECT_EQ(p.as_path().to_str().unwrap(), "/usr/bin"_str);
}

TEST(PathBuf, Push) {
#if RSTD_OS_WINDOWS
    auto p = PathBuf::from(R"(C:\usr)"_str);
#else
    auto p = PathBuf::from("/usr"_str);
#endif
    p.push(rstd::ref<Path>("bin"_str));
    auto s = p.as_path().to_str();
    ASSERT_TRUE(s.is_some());
#if RSTD_OS_WINDOWS
    EXPECT_EQ(*s, R"(C:\usr\bin)"_str);
#else
    EXPECT_EQ(*s, "/usr/bin"_str);
#endif
}

TEST(PathBuf, PushAbsolute) {
#if RSTD_OS_WINDOWS
    auto p = PathBuf::from(R"(C:\usr)"_str);
    p.push(rstd::ref<Path>(R"(D:\etc)"_str));
#else
    auto p = PathBuf::from("/usr"_str);
    p.push(rstd::ref<Path>("/etc"_str));
#endif
    auto s = p.as_path().to_str();
    ASSERT_TRUE(s.is_some());
#if RSTD_OS_WINDOWS
    EXPECT_EQ(*s, R"(D:\etc)"_str);
#else
    EXPECT_EQ(*s, "/etc"_str);
#endif
}

TEST(PathBuf, PushNoDoubleSep) {
#if RSTD_OS_WINDOWS
    auto p = PathBuf::from(R"(C:\usr\)"_str);
#else
    auto p = PathBuf::from("/usr/"_str);
#endif
    p.push(rstd::ref<Path>("bin"_str));
    auto s = p.as_path().to_str();
    ASSERT_TRUE(s.is_some());
#if RSTD_OS_WINDOWS
    EXPECT_EQ(*s, R"(C:\usr\bin)"_str);
#else
    EXPECT_EQ(*s, "/usr/bin"_str);
#endif
}

TEST(PathBuf, Pop) {
    auto p = PathBuf::from("/usr/bin/ls"_str);
    EXPECT_TRUE(p.pop());
    EXPECT_EQ(p.as_path().to_str().unwrap(), "/usr/bin"_str);
    EXPECT_TRUE(p.pop());
    EXPECT_EQ(p.as_path().to_str().unwrap(), "/usr"_str);
}

TEST(PathBuf, Join) {
#if RSTD_OS_WINDOWS
    auto p      = PathBuf::from(R"(C:\usr)"_str);
    auto joined = p.join(rstd::ref<Path>(R"(local\bin)"_str));
#else
    auto p      = PathBuf::from("/usr"_str);
    auto joined = p.join(rstd::ref<Path>("local/bin"_str));
#endif
    auto s = joined.as_path().to_str();
    ASSERT_TRUE(s.is_some());
#if RSTD_OS_WINDOWS
    EXPECT_EQ(*s, R"(C:\usr\local\bin)"_str);
#else
    EXPECT_EQ(*s, "/usr/local/bin"_str);
#endif
}

TEST(Path, LexicallyRelativePreservesCommonPrefix) {
#if RSTD_OS_WINDOWS
    auto base     = PathBuf::from(R"(C:\root\bin\nested)"_str);
    auto target   = PathBuf::from(R"(C:\root\packages\tool\bin\nested\tool.exe)"_str);
    auto relative = rstd::path::lexically_relative(base.as_path(), target.as_path());
    ASSERT_TRUE(relative.is_some());
    EXPECT_EQ(relative->as_path().to_str().unwrap(),
              R"(..\..\packages\tool\bin\nested\tool.exe)"_str);
#else
    auto base     = PathBuf::from("/root/bin/nested"_str);
    auto target   = PathBuf::from("/root/packages/tool/bin/nested/tool"_str);
    auto relative = rstd::path::lexically_relative(base.as_path(), target.as_path());
    ASSERT_TRUE(relative.is_some());
    EXPECT_EQ(relative->as_path().to_str().unwrap(), "../../packages/tool/bin/nested/tool"_str);
#endif
    auto same = rstd::path::lexically_relative(target.as_path(), target.as_path());
    ASSERT_TRUE(same.is_some());
    EXPECT_EQ(same->as_path().to_str().unwrap(), "."_str);
}

TEST(Path, LexicallyRelativeRejectsIncompatiblePaths) {
#if RSTD_OS_WINDOWS
    auto absolute = PathBuf::from(R"(C:\root\tool)"_str);
#else
    auto absolute = PathBuf::from("/root/tool"_str);
#endif
    auto relative = PathBuf::from("root/tool"_str);
    EXPECT_TRUE(rstd::path::lexically_relative(absolute.as_path(), relative.as_path()).is_none());
    EXPECT_TRUE(
        rstd::path::lexically_relative(rstd::ref<Path>("root/../tool"_str), relative.as_path())
            .is_none());
}

TEST(PathBuf, ImplicitConversion) {
    auto            buf = PathBuf::from("/tmp"_str);
    rstd::ref<Path> r   = buf;
    EXPECT_EQ(r.len(), rstd::usize(4));
}
