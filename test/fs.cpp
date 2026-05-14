#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <gtest/gtest.h>
import rstd;

using rstd::fs::File;
using rstd::fs::FileType;
using rstd::fs::OpenOptions;
using rstd::fs::Permissions;
using rstd::io::SeekFrom;

namespace
{

// RAII helper for a unique temp-file path under /tmp.
class TempPath {
public:
    TempPath() {
        char tmpl[] = "/tmp/rstd-fs-test-XXXXXX";
        int  fd     = ::mkstemp(tmpl);
        if (fd >= 0) ::close(fd);
        path_ = tmpl;
    }
    ~TempPath() { ::unlink(path_.c_str()); }
    auto c_str() const -> const char* { return path_.c_str(); }
    auto as_path() const -> rstd::ref<rstd::path::Path> {
        return rstd::ref<rstd::path::Path>(path_.c_str());
    }

private:
    std::string path_;
};

} // namespace

TEST(Fs, CreateWriteReadRoundTrip) {
    TempPath tp;
    {
        auto res = File::create(tp.as_path());
        ASSERT_TRUE(res.is_ok());
        auto f = rstd::move(res).unwrap_unchecked();

        const char* payload = "hello world";
        auto        wres    = f.write(reinterpret_cast<const rstd::u8*>(payload), 11);
        ASSERT_TRUE(wres.is_ok());
        EXPECT_EQ(wres.unwrap_unchecked(), 11u);

        auto sres = f.sync_all();
        EXPECT_TRUE(sres.is_ok());
    }

    {
        auto res = File::open(tp.as_path());
        ASSERT_TRUE(res.is_ok());
        auto f = rstd::move(res).unwrap_unchecked();

        rstd::u8 buf[32] = {};
        auto     rres    = f.read(buf, 32);
        ASSERT_TRUE(rres.is_ok());
        EXPECT_EQ(rres.unwrap_unchecked(), 11u);
        EXPECT_EQ(std::memcmp(buf, "hello world", 11), 0);
    }
}

TEST(Fs, OpenMissingReturnsNotFound) {
    rstd::ref<rstd::path::Path> p("/tmp/this-path-should-not-exist-rstd-test-xyz");
    auto                        res = File::open(p);
    ASSERT_TRUE(res.is_err());
    auto err = rstd::move(res).unwrap_err_unchecked();
    EXPECT_EQ(err.kind().code, rstd::io::error::ErrorKind::NotFound);
}

TEST(Fs, CreateNewFailsIfExists) {
    TempPath tp;
    auto     res = File::create_new(tp.as_path());
    ASSERT_TRUE(res.is_err());
    auto err = rstd::move(res).unwrap_err_unchecked();
    EXPECT_EQ(err.kind().code, rstd::io::error::ErrorKind::AlreadyExists);
}

TEST(Fs, SeekRoundTrip) {
    TempPath tp;
    auto     fres = OpenOptions::make()
                    .read(true)
                    .write(true)
                    .create(true)
                    .truncate(true)
                    .open(tp.as_path());
    ASSERT_TRUE(fres.is_ok());
    auto f = rstd::move(fres).unwrap_unchecked();

    const char* payload = "0123456789";
    f.write(reinterpret_cast<const rstd::u8*>(payload), 10).unwrap_unchecked();

    auto pos = f.seek(SeekFrom::from_start(4));
    ASSERT_TRUE(pos.is_ok());
    EXPECT_EQ(pos.unwrap_unchecked(), 4u);

    rstd::u8 buf[6] = {};
    auto     n      = f.read(buf, 6).unwrap_unchecked();
    EXPECT_EQ(n, 6u);
    EXPECT_EQ(std::memcmp(buf, "456789", 6), 0);

    auto end_pos = f.seek(SeekFrom::from_end(0));
    EXPECT_EQ(end_pos.unwrap_unchecked(), 10u);
}

TEST(Fs, SetLenTruncatesAndExtends) {
    TempPath tp;
    auto     f = File::create(tp.as_path()).unwrap_unchecked();
    f.write(reinterpret_cast<const rstd::u8*>("0123456789"), 10).unwrap_unchecked();

    EXPECT_TRUE(f.set_len(5).is_ok());

    struct stat st{};
    ::stat(tp.c_str(), &st);
    EXPECT_EQ(st.st_size, 5);

    EXPECT_TRUE(f.set_len(20).is_ok());
    ::stat(tp.c_str(), &st);
    EXPECT_EQ(st.st_size, 20);
}

TEST(Fs, TryCloneTwoHandlesSameFile) {
    TempPath tp;
    auto     f1 = OpenOptions::make()
                  .read(true)
                  .write(true)
                  .create(true)
                  .truncate(true)
                  .open(tp.as_path())
                  .unwrap_unchecked();
    auto f2 = f1.try_clone().unwrap_unchecked();
    EXPECT_NE(f1.as_raw_fd(), f2.as_raw_fd());

    f1.write(reinterpret_cast<const rstd::u8*>("ABCD"), 4).unwrap_unchecked();
    f1.sync_all().unwrap_unchecked();

    f2.seek(SeekFrom::from_start(0)).unwrap_unchecked();
    rstd::u8 buf[4] = {};
    f2.read(buf, 4).unwrap_unchecked();
    EXPECT_EQ(std::memcmp(buf, "ABCD", 4), 0);
}

TEST(Fs, AppendFlagPositionsAtEnd) {
    TempPath tp;
    File::create(tp.as_path())
        .unwrap_unchecked()
        .write(reinterpret_cast<const rstd::u8*>("AAA"), 3)
        .unwrap_unchecked();

    auto f = OpenOptions::make().append(true).open(tp.as_path()).unwrap_unchecked();
    f.write(reinterpret_cast<const rstd::u8*>("BBB"), 3).unwrap_unchecked();

    auto v = File::open(tp.as_path()).unwrap_unchecked();
    rstd::u8 buf[6] = {};
    auto     n      = v.read(buf, 6).unwrap_unchecked();
    EXPECT_EQ(n, 6u);
    EXPECT_EQ(std::memcmp(buf, "AAABBB", 6), 0);
}

TEST(FsMetadata, BasicStat) {
    TempPath tp;
    {
        auto f = File::create(tp.as_path()).unwrap_unchecked();
        f.write(reinterpret_cast<const rstd::u8*>("hi!"), 3).unwrap_unchecked();
    }
    auto f    = File::open(tp.as_path()).unwrap_unchecked();
    auto mres = f.metadata();
    ASSERT_TRUE(mres.is_ok());
    auto m = rstd::move(mres).unwrap_unchecked();
    EXPECT_TRUE(m.is_file());
    EXPECT_FALSE(m.is_dir());
    EXPECT_FALSE(m.is_symlink());
    EXPECT_EQ(m.len(), 3u);
    EXPECT_GT(m.ino(), 0u);
    EXPECT_NE(m.nlink(), 0u);
}

TEST(FsMetadata, DirectoryDetected) {
    auto f    = File::open(rstd::ref<rstd::path::Path>("/tmp")).unwrap_unchecked();
    auto m    = f.metadata().unwrap_unchecked();
    EXPECT_TRUE(m.is_dir());
    EXPECT_FALSE(m.is_file());
}

TEST(FsPermissions, ReadonlyToggle) {
    TempPath tp;
    {
        auto f = File::create(tp.as_path()).unwrap_unchecked();
        f.write(reinterpret_cast<const rstd::u8*>("a"), 1).unwrap_unchecked();
    }
    auto f0   = File::open(tp.as_path()).unwrap_unchecked();
    auto perm = f0.metadata().unwrap_unchecked().permissions();
    EXPECT_FALSE(perm.readonly());
    perm.set_readonly(true);
    EXPECT_TRUE(perm.readonly());

    auto f = OpenOptions::make().write(true).open(tp.as_path()).unwrap_unchecked();
    EXPECT_TRUE(f.set_permissions(Permissions::from_mode(0444)).is_ok());

    auto m = f.metadata().unwrap_unchecked();
    EXPECT_EQ(m.mode() & 0777u, 0444u);
    EXPECT_TRUE(m.permissions().readonly());
}

TEST(FsTimes, SetModifiedRoundTrip) {
    TempPath tp;
    auto     f = OpenOptions::make()
                 .read(true)
                 .write(true)
                 .create(true)
                 .truncate(true)
                 .open(tp.as_path())
                 .unwrap_unchecked();

    // Pick an epoch instant: 2020-01-01 UTC = 1577836800
    auto t   = rstd::time::SystemTime { rstd::sys::pal::unix::time::SystemTime {
        rstd::sys::pal::unix::time::Timespec { 1577836800, 0 } } };
    auto res = f.set_modified(t);
    ASSERT_TRUE(res.is_ok());

    auto m       = f.metadata().unwrap_unchecked();
    auto modres  = m.modified();
    ASSERT_TRUE(modres.is_ok());
    auto got_st = rstd::move(modres).unwrap_unchecked();
    EXPECT_EQ(got_st.inner.t.tv_sec, 1577836800);
}

TEST(FsFileType, EqualityForSameKind) {
    auto t1 = FileType { S_IFREG };
    auto t2 = FileType { S_IFREG | 0644 };
    auto td = FileType { S_IFDIR };
    EXPECT_TRUE(t1 == t2);
    EXPECT_FALSE(t1 == td);
}

TEST(FsFreeFn, WriteReadRoundTrip) {
    TempPath tp;
    const char* msg   = "hello fs::write";
    auto       slice = rstd::slice<rstd::u8>::from_raw_parts(
        const_cast<rstd::u8*>(reinterpret_cast<const rstd::u8*>(msg)), 15);
    ASSERT_TRUE(rstd::fs::write(tp.as_path(), slice).is_ok());

    auto v = rstd::fs::read(tp.as_path()).unwrap_unchecked();
    EXPECT_EQ(v.len(), 15u);
    auto s = rstd::fs::read_to_string(tp.as_path()).unwrap_unchecked();
    EXPECT_EQ(s.len(), 15u);
    EXPECT_EQ(std::memcmp(s.data(), msg, 15), 0);
}

TEST(FsFreeFn, MetadataAndExists) {
    TempPath tp;
    EXPECT_TRUE(rstd::fs::exists(tp.as_path()).unwrap_unchecked());
    auto m = rstd::fs::metadata(tp.as_path()).unwrap_unchecked();
    EXPECT_TRUE(m.is_file());

    rstd::ref<rstd::path::Path> nope("/tmp/rstd-fs-missing-xyz-test");
    EXPECT_FALSE(rstd::fs::exists(nope).unwrap_unchecked());
}

TEST(FsFreeFn, RemoveFile) {
    TempPath tp;
    EXPECT_TRUE(rstd::fs::exists(tp.as_path()).unwrap_unchecked());
    EXPECT_TRUE(rstd::fs::remove_file(tp.as_path()).is_ok());
    EXPECT_FALSE(rstd::fs::exists(tp.as_path()).unwrap_unchecked());
}

TEST(FsFreeFn, RenameMoves) {
    TempPath src;
    char     dst_buf[] = "/tmp/rstd-fs-rename-XXXXXX";
    int      fd        = ::mkstemp(dst_buf);
    ::close(fd);
    ::unlink(dst_buf); // we need the target to NOT exist

    EXPECT_TRUE(rstd::fs::rename(src.as_path(),
                                 rstd::ref<rstd::path::Path>(dst_buf))
                    .is_ok());
    EXPECT_FALSE(rstd::fs::exists(src.as_path()).unwrap_unchecked());
    EXPECT_TRUE(rstd::fs::exists(rstd::ref<rstd::path::Path>(dst_buf)).unwrap_unchecked());
    ::unlink(dst_buf);
}

TEST(FsFreeFn, CopyDuplicates) {
    TempPath src;
    auto     slice = rstd::slice<rstd::u8>::from_raw_parts(
        const_cast<rstd::u8*>(reinterpret_cast<const rstd::u8*>("ABCDE")), 5);
    rstd::fs::write(src.as_path(), slice).unwrap_unchecked();

    char dst_buf[] = "/tmp/rstd-fs-copy-XXXXXX";
    int  fd        = ::mkstemp(dst_buf);
    ::close(fd);

    auto n = rstd::fs::copy(src.as_path(),
                            rstd::ref<rstd::path::Path>(dst_buf))
                 .unwrap_unchecked();
    EXPECT_EQ(n, 5u);
    auto v = rstd::fs::read(rstd::ref<rstd::path::Path>(dst_buf)).unwrap_unchecked();
    EXPECT_EQ(v.len(), 5u);
    ::unlink(dst_buf);
}

TEST(FsFreeFn, CreateAndRemoveDir) {
    char dir_buf[] = "/tmp/rstd-fs-dir-XXXXXX";
    auto p         = ::mkdtemp(dir_buf);
    ASSERT_NE(p, nullptr);
    // mkdtemp already created it; remove and recreate via our API.
    ::rmdir(dir_buf);
    EXPECT_TRUE(rstd::fs::create_dir(rstd::ref<rstd::path::Path>(dir_buf)).is_ok());
    EXPECT_TRUE(rstd::fs::metadata(rstd::ref<rstd::path::Path>(dir_buf))
                    .unwrap_unchecked()
                    .is_dir());
    EXPECT_TRUE(rstd::fs::remove_dir(rstd::ref<rstd::path::Path>(dir_buf)).is_ok());
}

TEST(FsFreeFn, CreateDirAllNested) {
    char base[] = "/tmp/rstd-fs-cda-XXXXXX";
    ::mkdtemp(base);

    rstd::path::PathBuf p = rstd::path::PathBuf::from(base);
    p.push(rstd::ref<rstd::path::Path>("a"));
    p.push(rstd::ref<rstd::path::Path>("b"));
    p.push(rstd::ref<rstd::path::Path>("c"));

    EXPECT_TRUE(rstd::fs::create_dir_all(p).is_ok());
    EXPECT_TRUE(rstd::fs::metadata(p).unwrap_unchecked().is_dir());

    // Cleanup: just rmdir leaf-up via shell.
    while (p.pop() && p.as_path().len() > rstd::ref<rstd::path::Path>(base).len()) {
        rstd::fs::remove_dir(p).is_ok();
    }
    ::rmdir(base);
}

TEST(FsFreeFn, ReadLink) {
    char src[] = "/tmp/rstd-fs-symtgt-XXXXXX";
    int  fd    = ::mkstemp(src);
    ::close(fd);
    char link_path[] = "/tmp/rstd-fs-symlnk-XXXXXX";
    int  lf          = ::mkstemp(link_path);
    ::close(lf);
    ::unlink(link_path);

    EXPECT_TRUE(rstd::fs::soft_link(rstd::ref<rstd::path::Path>(src),
                                    rstd::ref<rstd::path::Path>(link_path))
                    .is_ok());
    auto target = rstd::fs::read_link(rstd::ref<rstd::path::Path>(link_path))
                      .unwrap_unchecked();
    EXPECT_EQ(target.len(), std::strlen(src));

    ::unlink(link_path);
    ::unlink(src);
}

TEST(FsFreeFn, SetPermissionsByPath) {
    TempPath tp;
    EXPECT_TRUE(rstd::fs::set_permissions(tp.as_path(), Permissions::from_mode(0600)).is_ok());
    auto m = rstd::fs::metadata(tp.as_path()).unwrap_unchecked();
    EXPECT_EQ(m.mode() & 0777u, 0600u);
}

TEST(FsReadDir, IteratesEntries) {
    char base[] = "/tmp/rstd-fs-readdir-XXXXXX";
    ::mkdtemp(base);

    // Create two files under base.
    rstd::path::PathBuf p1 = rstd::path::PathBuf::from(base);
    p1.push(rstd::ref<rstd::path::Path>("a"));
    rstd::fs::write(p1, rstd::slice<rstd::u8>::from_raw_parts(
                            const_cast<rstd::u8*>(reinterpret_cast<const rstd::u8*>("x")), 1))
        .unwrap_unchecked();

    rstd::path::PathBuf p2 = rstd::path::PathBuf::from(base);
    p2.push(rstd::ref<rstd::path::Path>("b"));
    rstd::fs::write(p2, rstd::slice<rstd::u8>::from_raw_parts(
                            const_cast<rstd::u8*>(reinterpret_cast<const rstd::u8*>("y")), 1))
        .unwrap_unchecked();

    auto rd = rstd::fs::read_dir(rstd::ref<rstd::path::Path>(base)).unwrap_unchecked();
    int  count = 0;
    while (true) {
        auto opt = rd.next();
        if (opt.is_none()) break;
        auto er = rstd::move(opt).unwrap_unchecked();
        ASSERT_TRUE(er.is_ok());
        auto e = rstd::move(er).unwrap_unchecked();
        EXPECT_TRUE(e.file_type().unwrap_unchecked().is_file());
        count++;
    }
    EXPECT_EQ(count, 2);

    rstd::fs::remove_dir_all(rstd::ref<rstd::path::Path>(base)).unwrap_unchecked();
    EXPECT_FALSE(rstd::fs::exists(rstd::ref<rstd::path::Path>(base)).unwrap_unchecked());
}

TEST(FsReadDir, RemoveDirAllRecursive) {
    char base[] = "/tmp/rstd-fs-rda-XXXXXX";
    ::mkdtemp(base);

    rstd::path::PathBuf p = rstd::path::PathBuf::from(base);
    p.push(rstd::ref<rstd::path::Path>("sub"));
    p.push(rstd::ref<rstd::path::Path>("deeper"));
    rstd::fs::create_dir_all(p).unwrap_unchecked();

    rstd::path::PathBuf leaf = rstd::path::PathBuf::from(base);
    leaf.push(rstd::ref<rstd::path::Path>("sub"));
    leaf.push(rstd::ref<rstd::path::Path>("deeper"));
    leaf.push(rstd::ref<rstd::path::Path>("leaf"));
    rstd::fs::write(leaf, rstd::slice<rstd::u8>::from_raw_parts(
                              const_cast<rstd::u8*>(reinterpret_cast<const rstd::u8*>("z")), 1))
        .unwrap_unchecked();

    EXPECT_TRUE(rstd::fs::remove_dir_all(rstd::ref<rstd::path::Path>(base)).is_ok());
    EXPECT_FALSE(rstd::fs::exists(rstd::ref<rstd::path::Path>(base)).unwrap_unchecked());
}

TEST(FsFile, ReadAtWriteAt) {
    TempPath tp;
    auto     f = OpenOptions::make()
                 .read(true)
                 .write(true)
                 .create(true)
                 .truncate(true)
                 .open(tp.as_path())
                 .unwrap_unchecked();

    EXPECT_TRUE(f.write_all_at(reinterpret_cast<const rstd::u8*>("HELLO"), 5, 100).is_ok());
    rstd::u8 buf[5] = {};
    EXPECT_TRUE(f.read_exact_at(buf, 5, 100).is_ok());
    EXPECT_EQ(std::memcmp(buf, "HELLO", 5), 0);
}

TEST(FsFile, FlockExclusiveBlocks) {
    TempPath tp;
    auto     f1 = File::create(tp.as_path()).unwrap_unchecked();
    auto     f2 = File::open(tp.as_path()).unwrap_unchecked();
    ASSERT_TRUE(f1.try_lock().is_ok());

    auto r = f2.try_lock();
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(rstd::move(r).unwrap_err_unchecked().kind().code,
              rstd::io::error::ErrorKind::WouldBlock);

    EXPECT_TRUE(f1.unlock().is_ok());
    EXPECT_TRUE(f2.try_lock().is_ok());
    EXPECT_TRUE(f2.unlock().is_ok());
}

TEST(Fs, ReadTraitImplCanBeUsed) {
    TempPath tp;
    File::create(tp.as_path())
        .unwrap_unchecked()
        .write(reinterpret_cast<const rstd::u8*>("xyz"), 3)
        .unwrap_unchecked();

    auto f = File::open(tp.as_path()).unwrap_unchecked();
    rstd::u8 buf[8] = {};
    auto     res    = rstd::as<rstd::io::Read>(f).read(buf, 8);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), 3u);
}
