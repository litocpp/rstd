#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;

using rstd::fs::File;
using rstd::fs::OpenOptions;
using rstd::io::SeekFrom;
using namespace rstd::literals;

namespace
{

auto child_path(rstd::ref<rstd::path::Path> parent, rstd::ref<rstd::str> name)
    -> rstd::path::PathBuf {
    auto component = rstd::path::PathBuf::from(name);
    return rstd::path::PathBuf::from(parent).join(component.as_path());
}

} // namespace

TEST(FsWindows, CreateWriteReadSeekAndResize) {
    auto temporary = rstd::test::TempDir::make().unwrap();
    auto path      = child_path(temporary.path(), "data.bin"_str);
    auto file =
        OpenOptions::make().read(true).write(true).create_new(true).open(path.as_path()).unwrap();

    file.write_all("hello windows"_bytes).unwrap();
    EXPECT_EQ(file.metadata()->len(), rstd::u64(13));

    file.seek(SeekFrom::from_start(rstd::u64(6))).unwrap();
    auto tail = rstd::array<rstd::u8, 7> {};
    EXPECT_EQ(file.read(tail.as_mut_slice()).unwrap(), rstd::usize(7));
    EXPECT_TRUE(tail.as_slice() == "windows"_bytes);

    file.set_len(rstd::u64(5)).unwrap();
    EXPECT_EQ(file.metadata()->len(), rstd::u64(5));
    EXPECT_EQ(rstd::fs::read_to_string(path.as_path()).unwrap(), "hello"_str);
}

TEST(FsWindows, DirectoryCopyRenameAndHardLink) {
    auto temporary = rstd::test::TempDir::make().unwrap();
    auto nested    = child_path(temporary.path(), "nested"_str);
    auto deeper    = child_path(nested.as_path(), "deeper"_str);
    rstd::fs::create_dir_all(deeper.as_path()).unwrap();

    auto source = child_path(deeper.as_path(), "source.txt"_str);
    rstd::fs::write(source.as_path(), "payload"_bytes).unwrap();

    auto copied = child_path(deeper.as_path(), "copied.txt"_str);
    EXPECT_EQ(rstd::fs::copy(source.as_path(), copied.as_path()).unwrap(), rstd::u64(7));
    EXPECT_EQ(rstd::fs::read_to_string(copied.as_path()).unwrap(), "payload"_str);

    auto renamed = child_path(deeper.as_path(), "renamed.txt"_str);
    rstd::fs::rename(copied.as_path(), renamed.as_path()).unwrap();
    EXPECT_FALSE(rstd::fs::exists(copied.as_path()).unwrap());
    EXPECT_TRUE(rstd::fs::exists(renamed.as_path()).unwrap());

    auto linked = child_path(deeper.as_path(), "linked.txt"_str);
    rstd::fs::hard_link(source.as_path(), linked.as_path()).unwrap();
    EXPECT_EQ(rstd::fs::read_to_string(linked.as_path()).unwrap(), "payload"_str);

    auto entries = rstd::fs::read_dir(deeper.as_path()).unwrap();
    auto count   = rstd::usize {};
    while (auto entry = entries.next()) {
        EXPECT_TRUE(entry->is_ok());
        ++count;
    }
    EXPECT_EQ(count, rstd::usize(3));
}

TEST(FsWindows, ReadDirMoveTransfersDirectoryState) {
    auto temporary = rstd::test::TempDir::make().unwrap();
    auto source    = child_path(temporary.path(), "source"_str);
    auto target    = child_path(temporary.path(), "target"_str);
    rstd::fs::create_dir(source.as_path()).unwrap();
    rstd::fs::create_dir(target.as_path()).unwrap();

    auto entry = child_path(source.as_path(), "entry"_str);
    rstd::fs::write(entry.as_path(), "x"_bytes).unwrap();

    auto source_entries = rstd::fs::read_dir(source.as_path()).unwrap();
    auto moved_entries  = rstd::move(source_entries);
    auto target_entries = rstd::fs::read_dir(target.as_path()).unwrap();
    target_entries      = rstd::move(moved_entries);

    EXPECT_TRUE(source_entries.next().is_none());
    EXPECT_TRUE(moved_entries.next().is_none());
    ASSERT_TRUE(target_entries.next().is_some());
    EXPECT_TRUE(target_entries.next().is_none());
}

TEST(FsWindows, AtomicWriteReportsChanges) {
    auto temporary = rstd::test::TempDir::make().unwrap();
    auto path      = child_path(temporary.path(), "atomic.txt"_str);

    EXPECT_EQ(rstd::fs::write_atomic_if_changed(path.as_path(), "first"_bytes).unwrap(),
              rstd::fs::WriteOutcome::Created);
    EXPECT_EQ(rstd::fs::write_atomic_if_changed(path.as_path(), "first"_bytes).unwrap(),
              rstd::fs::WriteOutcome::Unchanged);
    EXPECT_EQ(rstd::fs::write_atomic_if_changed(path.as_path(), "second"_bytes).unwrap(),
              rstd::fs::WriteOutcome::Replaced);
    EXPECT_EQ(rstd::fs::read_to_string(path.as_path()).unwrap(), "second"_str);
}

TEST(FsWindows, MissingFileReturnsNotFound) {
    auto temporary = rstd::test::TempDir::make().unwrap();
    auto path      = child_path(temporary.path(), "missing.txt"_str);
    auto result    = File::open(path.as_path());
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind().code, rstd::io::error::ErrorKind::NotFound);
}
