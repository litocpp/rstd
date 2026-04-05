import rstd;
#include <gtest/gtest.h>

using rstd::path::Path;
using rstd::path::PathBuf;

// ── ref<Path> ────────────────────────────────────────────────────────────

TEST(Path, IsAbsolute) {
    EXPECT_TRUE(rstd::ref<Path>("/usr/bin").is_absolute());
    EXPECT_FALSE(rstd::ref<Path>("relative/path").is_absolute());
    EXPECT_FALSE(rstd::ref<Path>("").is_absolute());
}

TEST(Path, Parent) {
    auto p = rstd::ref<Path>("/usr/bin/ls").parent();
    ASSERT_TRUE(p.is_some());
    auto parent = (*p).to_str();
    ASSERT_TRUE(parent.is_some());
    EXPECT_EQ(*parent, rstd::ref<rstd::str>("/usr/bin"));
}

TEST(Path, ParentRoot) {
    // "/" has no parent (it IS the root)
    auto p = rstd::ref<Path>("/").parent();
    EXPECT_TRUE(p.is_none());
}

TEST(Path, ParentRelative) {
    auto p = rstd::ref<Path>("foo/bar").parent();
    ASSERT_TRUE(p.is_some());
    EXPECT_EQ((*p).to_str().unwrap(), rstd::ref<rstd::str>("foo"));
}

TEST(Path, ParentNoSep) {
    // Single component, no parent
    EXPECT_TRUE(rstd::ref<Path>("file.txt").parent().is_none());
}

TEST(Path, FileName) {
    auto f = rstd::ref<Path>("/usr/bin/ls").file_name();
    ASSERT_TRUE(f.is_some());
    EXPECT_EQ((*f).to_str().unwrap(), rstd::ref<rstd::str>("ls"));
}

TEST(Path, FileNameDir) {
    auto f = rstd::ref<Path>("/usr/bin/").file_name();
    ASSERT_TRUE(f.is_some());
    EXPECT_EQ((*f).to_str().unwrap(), rstd::ref<rstd::str>("bin"));
}

TEST(Path, Extension) {
    auto e = rstd::ref<Path>("file.tar.gz").extension();
    ASSERT_TRUE(e.is_some());
    EXPECT_EQ((*e).to_str().unwrap(), rstd::ref<rstd::str>("gz"));
}

TEST(Path, ExtensionNone) {
    EXPECT_TRUE(rstd::ref<Path>("Makefile").extension().is_none());
}

TEST(Path, ExtensionDotFile) {
    // ".gitignore" — the leading dot is not an extension separator
    EXPECT_TRUE(rstd::ref<Path>(".gitignore").extension().is_none());
}

TEST(Path, ToStr) {
    auto s = rstd::ref<Path>("/tmp/test").to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(*s, rstd::ref<rstd::str>("/tmp/test"));
}

// ── PathBuf ──────────────────────────────────────────────────────────────

TEST(PathBuf, MakeEmpty) {
    auto p = PathBuf::make();
    EXPECT_TRUE(p.is_empty());
}

TEST(PathBuf, FromStr) {
    auto p = PathBuf::from("/usr/bin");
    EXPECT_EQ(p.len(), 8u);
    EXPECT_TRUE(p.as_path().is_absolute());
}

TEST(PathBuf, Push) {
    auto p = PathBuf::from("/usr");
    p.push(rstd::ref<Path>("bin"));
    auto s = p.as_path().to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(*s, rstd::ref<rstd::str>("/usr/bin"));
}

TEST(PathBuf, PushAbsolute) {
    auto p = PathBuf::from("/usr");
    p.push(rstd::ref<Path>("/etc"));
    auto s = p.as_path().to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(*s, rstd::ref<rstd::str>("/etc"));
}

TEST(PathBuf, PushNoDoubleSep) {
    auto p = PathBuf::from("/usr/");
    p.push(rstd::ref<Path>("bin"));
    auto s = p.as_path().to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(*s, rstd::ref<rstd::str>("/usr/bin"));
}

TEST(PathBuf, Pop) {
    auto p = PathBuf::from("/usr/bin/ls");
    EXPECT_TRUE(p.pop());
    EXPECT_EQ(p.as_path().to_str().unwrap(), rstd::ref<rstd::str>("/usr/bin"));
    EXPECT_TRUE(p.pop());
    EXPECT_EQ(p.as_path().to_str().unwrap(), rstd::ref<rstd::str>("/usr"));
}

TEST(PathBuf, Join) {
    auto p = PathBuf::from("/usr");
    auto joined = p.join(rstd::ref<Path>("local/bin"));
    auto s = joined.as_path().to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(*s, rstd::ref<rstd::str>("/usr/local/bin"));
}

TEST(PathBuf, ImplicitConversion) {
    auto buf = PathBuf::from("/tmp");
    rstd::ref<Path> r = buf;
    EXPECT_EQ(r.len(), 4u);
}
