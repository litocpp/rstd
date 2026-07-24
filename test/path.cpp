#include <gtest/gtest.h>
import rstd;

using rstd::path::Component;
using rstd::path::Path;
using rstd::path::PathBuf;
using namespace rstd::literals;

// ── ref<Path> ────────────────────────────────────────────────────────────

TEST(Path, IsAbsolute) {
    EXPECT_TRUE(rstd::ref<Path>("/usr/bin"_str).is_absolute());
    EXPECT_FALSE(rstd::ref<Path>("relative/path"_str).is_absolute());
    EXPECT_FALSE(rstd::ref<Path>(""_str).is_absolute());
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
    // Single component, no parent
    EXPECT_TRUE(rstd::ref<Path>("file.txt"_str).parent().is_none());
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
}

static_assert(rstd::Impled<PathBuf, rstd::clone::Clone>);

TEST(PathBuf, CloneOwnsIndependentStorage) {
    auto original = PathBuf::from("/tmp/original"_str);
    auto cloned   = original.clone();
    cloned.push(rstd::ref<Path>("child"_str));

    EXPECT_EQ(original.as_path().to_str().unwrap(), "/tmp/original"_str);
    EXPECT_EQ(cloned.as_path().to_str().unwrap(), "/tmp/original/child"_str);
}

TEST(PathBuf, FromStr) {
    auto p = PathBuf::from("/usr/bin"_str);
    EXPECT_EQ(p.len(), rstd::usize(8));
    EXPECT_TRUE(p.as_path().is_absolute());
}

TEST(PathBuf, FromPath) {
    auto p = PathBuf::from(rstd::ref<Path>("/usr/bin"_str));
    EXPECT_EQ(p.as_path().to_str().unwrap(), "/usr/bin"_str);
}

TEST(PathBuf, Push) {
    auto p = PathBuf::from("/usr"_str);
    p.push(rstd::ref<Path>("bin"_str));
    auto s = p.as_path().to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(*s, "/usr/bin"_str);
}

TEST(PathBuf, PushAbsolute) {
    auto p = PathBuf::from("/usr"_str);
    p.push(rstd::ref<Path>("/etc"_str));
    auto s = p.as_path().to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(*s, "/etc"_str);
}

TEST(PathBuf, PushNoDoubleSep) {
    auto p = PathBuf::from("/usr/"_str);
    p.push(rstd::ref<Path>("bin"_str));
    auto s = p.as_path().to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(*s, "/usr/bin"_str);
}

TEST(PathBuf, Pop) {
    auto p = PathBuf::from("/usr/bin/ls"_str);
    EXPECT_TRUE(p.pop());
    EXPECT_EQ(p.as_path().to_str().unwrap(), "/usr/bin"_str);
    EXPECT_TRUE(p.pop());
    EXPECT_EQ(p.as_path().to_str().unwrap(), "/usr"_str);
}

TEST(PathBuf, Join) {
    auto p      = PathBuf::from("/usr"_str);
    auto joined = p.join(rstd::ref<Path>("local/bin"_str));
    auto s      = joined.as_path().to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(*s, "/usr/local/bin"_str);
}

TEST(PathBuf, ImplicitConversion) {
    auto            buf = PathBuf::from("/tmp"_str);
    rstd::ref<Path> r   = buf;
    EXPECT_EQ(r.len(), rstd::usize(4));
}
