#include <rstd/macro.hpp>
#include <stdio.h>
#if RSTD_OS_UNIX
#include <fcntl.h>
#elif RSTD_OS_WINDOWS
#include <windows.h>
#endif
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::fs::Dir;
using rstd::fs::OpenOptions;
using rstd::path::Path;

#define CHECK(expression)                                                    \
    do {                                                                     \
        if (! (expression)) {                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                        \
        }                                                                    \
    } while (false)

int main() {
#if RSTD_OS_UNIX
    auto temporary = rstd::fs::TempDir::make("rstd-dir"_str).unwrap();
    auto root      = rstd::path::PathBuf::from(temporary.path().as_os_str().to_os_string());
    auto dir       = Dir::open(temporary.path()).unwrap();
    CHECK(dir.self_metadata().unwrap().is_dir());
    CHECK(Dir::open_with(temporary.path(), OpenOptions::make()).is_err());
    CHECK(Dir::open_with(temporary.path(), OpenOptions::make().read(true)).is_ok());
    CHECK((fcntl(dir.as_raw_fd(), F_GETFD) & FD_CLOEXEC) != 0);
    CHECK(dir.create_dir("sub"_str).is_ok());
    auto file = dir.open_file_with("sub/file"_str, OpenOptions::make().write(true).create_new(true))
                    .unwrap();
    CHECK(file.write_all("contents"_bytes).is_ok());
    CHECK(dir.open_file_with("sub/file"_str, OpenOptions::make().write(true).create_new(true))
              .is_err());
    CHECK(
        dir.open_file_with("sub/file"_str, OpenOptions::make().read(true).truncate(true)).is_err());
    auto masked = dir.open_file_with("sub/file"_str,
                                     OpenOptions::make().read(true).custom_flags(i32(O_WRONLY)))
                      .unwrap();
    CHECK((fcntl(masked.as_raw_fd(), F_GETFL) & O_ACCMODE) == O_RDONLY);
    array<u8, 8> contents {};
    CHECK(masked.read(contents.as_mut_slice()).unwrap() == usize(8));
    CHECK(contents.as_slice() == "contents"_bytes);
    CHECK(dir.open_dir("sub/file"_str).is_err());
    CHECK(dir.open_dir("sub"_str).is_ok());
    CHECK(dir.open_dir_with("sub"_str, OpenOptions::make().read(true)).is_ok());
    CHECK(dir.open_file("sub/../sub/file"_str).is_ok());
    CHECK(dir.open_dir("."_str).unwrap().self_metadata().unwrap().ino() ==
          dir.self_metadata().unwrap().ino());
    CHECK(dir.metadata("sub/file"_str).unwrap().len() == u64(8));
    auto absolute = root.join("sub/file"_str);
    auto direct   = OpenOptions::make()
                        .read(true)
                        .custom_flags(i32(O_WRONLY))
                        .open(absolute.as_path())
                        .unwrap();
    CHECK((fcntl(direct.as_raw_fd(), F_GETFL) & O_ACCMODE) == O_RDONLY);
    CHECK(dir.open_file(absolute.as_path()).is_ok());
    CHECK(dir.open_file("/dev/null"_str).is_ok());
    CHECK(dir.open_file(""_str).is_err());
    CHECK(dir.open_file("sub/file\0ignored"_str).is_err());
    auto missing = dir.metadata("missing"_str);
    CHECK(missing.is_err());
    CHECK(missing.unwrap_err().kind() ==
          rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound });

    auto link = root.join("link"_str);
    CHECK(rstd::fs::soft_link("sub/file"_str, link.as_path()).is_ok());
    CHECK(dir.metadata("link"_str).unwrap().is_file());
    CHECK(dir.symlink_metadata("link"_str).unwrap().is_symlink());
    CHECK(dir.open_file("link"_str).is_ok());
    CHECK(
        dir.open_file_with("link"_str, OpenOptions::make().read(true).custom_flags(i32(O_NOFOLLOW)))
            .is_err());
    auto dir_link = root.join("dir-link"_str);
    CHECK(rstd::fs::soft_link("sub"_str, dir_link.as_path()).is_ok());
    CHECK(Dir::open(dir_link.as_path()).is_ok());
    CHECK(Dir::open_with(dir_link.as_path(),
                         OpenOptions::make().read(true).custom_flags(i32(O_NOFOLLOW)))
              .is_err());
    CHECK(dir.open_dir("dir-link"_str).is_ok());
    CHECK(dir.remove_dir("dir-link"_str).is_err());
    CHECK(dir.remove_file("dir-link"_str).is_ok());
    CHECK(dir.metadata("sub"_str).unwrap().is_dir());

    auto traversal = Dir::open_for_traversal(temporary.path()).unwrap();
    CHECK(traversal.open_file("sub/file"_str).is_ok());
    CHECK(traversal.self_metadata().unwrap().ino() == dir.self_metadata().unwrap().ino());
#if RSTD_OS_LINUX
    CHECK((fcntl(traversal.as_raw_fd(), F_GETFL) & O_PATH) != 0);
#endif
    CHECK(dir.create_dir("other"_str).is_ok());
    auto other  = dir.open_dir("other"_str).unwrap();
    auto sub    = dir.open_dir("sub"_str).unwrap();
    auto copied = sub.try_clone().unwrap();
    CHECK(dir.rename("sub"_str, dir, "moved"_str).is_ok());
    CHECK(sub.open_file("file"_str).is_ok());
    CHECK(copied.open_file("file"_str).is_ok());
    CHECK(sub.open_file_with("file"_str, OpenOptions::make().append(true))
              .unwrap()
              .write_all("!"_bytes)
              .is_ok());
    CHECK(sub.metadata("file"_str).unwrap().len() == u64(9));
    CHECK(sub.open_file_with("file"_str, OpenOptions::make().write(true).truncate(true)).is_ok());
    CHECK(sub.metadata("file"_str).unwrap().len() == u64());
    CHECK(other.open_file_with("target"_str, OpenOptions::make().write(true).create(true)).is_ok());
    CHECK(sub.rename("file"_str, other, "target"_str).is_ok());
    CHECK(sub.metadata("file"_str).is_err());
    CHECK(other.metadata("target"_str).unwrap().is_file());
    CHECK(dir.remove_dir("other"_str).is_err());
    CHECK(other.remove_file("target"_str).is_ok());
    CHECK(dir.remove_dir("other"_str).is_ok());
    CHECK(dir.remove_file("moved"_str).is_err());
    CHECK(dir.remove_dir("moved"_str).is_ok());
    CHECK(dir.remove_file("link"_str).is_ok());
    auto raw     = rstd::move(dir.try_clone().unwrap()).into_raw_fd();
    auto adopted = Dir::from_raw_fd(raw);
    CHECK(adopted.as_fd().as_raw_fd() == raw);
    CHECK(adopted.self_metadata().unwrap().ino() == dir.self_metadata().unwrap().ino());
    puts("fs::Dir paths, options, links, handles and mutations passed");
#elif RSTD_OS_WINDOWS
    auto temporary = rstd::fs::TempDir::make("rstd-dir"_str).unwrap();
    auto root      = rstd::path::PathBuf::from(temporary.path().as_os_str().to_os_string());
    auto dir       = Dir::open(temporary.path()).unwrap();
    CHECK(dir.self_metadata().unwrap().is_dir());
    CHECK(Dir::open_with(temporary.path(), OpenOptions::make()).is_err());
    CHECK(Dir::open_with(temporary.path(), OpenOptions::make().read(true)).is_ok());
    DWORD handle_flags {};
    CHECK(GetHandleInformation(dir.as_raw_fd(), &handle_flags));
    CHECK((handle_flags & HANDLE_FLAG_INHERIT) == 0);
    CHECK(dir.create_dir("sub"_str).is_ok());
    CHECK(dir.create_dir("sub"_str).is_err());
    {
        auto file =
            dir.open_file_with("sub\\file"_str, OpenOptions::make().write(true).create_new(true))
                .unwrap();
        CHECK(file.write_all("contents"_bytes).is_ok());
        auto renamed = dir.rename("sub"_str, dir, "moved"_str);
        CHECK(renamed.is_err());
        CHECK(renamed.unwrap_err().raw_os_error() == Some(i32(ERROR_ACCESS_DENIED)));
    }
    CHECK(dir.open_file_with("sub\\file"_str, OpenOptions::make().write(true).create_new(true))
              .is_err());
    CHECK(dir.open_file_with("sub\\file"_str, OpenOptions::make().read(true).truncate(true))
              .is_err());
    CHECK(dir.open_dir("sub\\file"_str).is_err());
    CHECK(dir.open_file("sub"_str).is_err());
    CHECK(dir.open_dir_with("sub"_str, OpenOptions::make().read(true)).is_ok());
    auto absolute = root.join("sub\\file"_str);
    CHECK(Dir::open(absolute.as_path()).is_err());
    CHECK(Dir::open_for_traversal(absolute.as_path()).is_err());
    CHECK(Dir::open_with(absolute.as_path(), OpenOptions::make().write(true).truncate(true))
              .is_err());
    CHECK(dir.metadata("sub\\file"_str).unwrap().len() == u64(8));
    CHECK(dir.open_file(absolute.as_path()).is_ok());
    array<u8, 8> contents {};
    CHECK(dir.open_file("sub\\file"_str).unwrap().read(contents.as_mut_slice()).unwrap() ==
          usize(8));
    CHECK(contents.as_slice() == "contents"_bytes);
    CHECK(dir.open_file(""_str).is_err());
    CHECK(dir.open_file("sub\\file\0ignored"_str).is_err());
    CHECK(dir.metadata("missing"_str).unwrap_err().kind() ==
          rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound });
    auto too_long = Vec<u16>::with_capacity(usize(32768));
    too_long.resize(usize(32768), u16('x'));
    auto long_name = rstd::path::PathBuf::from(
        rstd::os::windows::ffi::OsStringExt::from_wide(too_long.as_slice()));
    CHECK(dir.open_file(long_name.as_path()).is_err());
    CHECK(dir.open_file_with("文件"_str, OpenOptions::make().write(true).create_new(true)).is_ok());
    CHECK(dir.remove_file("文件"_str).is_ok());
    array<u16, 2> surrogate { u16(0xd800), u16('x') };
    auto          native_name = rstd::path::PathBuf::from(
        rstd::os::windows::ffi::OsStringExt::from_wide(surrogate.as_slice()));
    CHECK(
        dir.open_file_with(native_name.as_path(), OpenOptions::make().write(true).create_new(true))
            .is_ok());
    CHECK(dir.metadata(native_name.as_path()).unwrap().is_file());
    CHECK(dir.remove_file(native_name.as_path()).is_ok());

    auto traversal = Dir::open_for_traversal(temporary.path()).unwrap();
    CHECK(traversal.open_file("sub\\file"_str).is_ok());
    CHECK(dir.create_dir("other"_str).is_ok());
    auto other  = dir.open_dir("other"_str).unwrap();
    auto sub    = dir.open_dir("sub"_str).unwrap();
    auto copied = sub.try_clone().unwrap();
    CHECK(GetHandleInformation(copied.as_raw_fd(), &handle_flags));
    CHECK((handle_flags & HANDLE_FLAG_INHERIT) == 0);
    CHECK(dir.rename("sub"_str, dir, "moved"_str).is_ok());
    CHECK(sub.open_file("file"_str).is_ok());
    CHECK(copied.open_file("file"_str).is_ok());
    CHECK(sub.open_file_with("file"_str, OpenOptions::make().write(true).append(true))
              .unwrap()
              .write_all("!"_bytes)
              .is_ok());
    CHECK(sub.metadata("file"_str).unwrap().len() == u64(9));
    auto moved_file = root.join("moved\\file"_str);
    CHECK(OpenOptions::make()
              .write(true)
              .append(true)
              .open(moved_file.as_path())
              .unwrap()
              .write_all("!"_bytes)
              .is_ok());
    CHECK(sub.metadata("file"_str).unwrap().len() == u64(10));
    CHECK(sub.open_file_with("file"_str, OpenOptions::make().write(true).truncate(true)).is_ok());
    CHECK(sub.metadata("file"_str).unwrap().len() == u64());
    CHECK(other.open_file_with("target"_str, OpenOptions::make().write(true).create(true)).is_ok());
    CHECK(sub.rename("file"_str, other, "target"_str).is_ok());
    CHECK(sub.metadata("file"_str).is_err());
    CHECK(other.metadata("target"_str).unwrap().is_file());

    auto target = root.join("other\\target"_str);
    auto link   = root.join("link"_str);
    CHECK(rstd::fs::soft_link(target.as_path(), link.as_path()).is_ok());
    CHECK(dir.metadata("link"_str).unwrap().is_file());
    CHECK(dir.symlink_metadata("link"_str).unwrap().is_symlink());
    CHECK(dir.open_file_with(
                 "link"_str,
                 OpenOptions::make().read(true).custom_flags(i32(FILE_FLAG_OPEN_REPARSE_POINT)))
              .unwrap()
              .metadata()
              .unwrap()
              .is_symlink());
    CHECK(dir.open_dir_with(
                 "other"_str,
                 OpenOptions::make().read(true).custom_flags(i32(FILE_FLAG_BACKUP_SEMANTICS)))
              .is_ok());
    CHECK(dir.open_file_with("link"_str,
                             OpenOptions::make().read(true).custom_flags(i32(FILE_FLAG_OVERLAPPED)))
              .unwrap_err()
              .kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::Unsupported });
    CHECK(dir.rename("link"_str, dir, "renamed-link"_str).is_ok());
    CHECK(dir.symlink_metadata("renamed-link"_str).unwrap().is_symlink());
    CHECK(dir.remove_file("renamed-link"_str).is_ok());
    CHECK(other.metadata("target"_str).unwrap().is_file());
    auto dir_target = root.join("other"_str);
    CHECK(rstd::fs::soft_link(dir_target.as_path(), link.as_path()).is_ok());
    CHECK(dir.open_dir("link"_str).is_ok());
    CHECK(dir.remove_dir("link"_str).is_ok());
    CHECK(dir.metadata("other"_str).unwrap().is_dir());
    CHECK(dir.remove_dir("other"_str).is_err());
    CHECK(other.remove_file("target"_str).is_ok());
    CHECK(dir.remove_dir("other"_str).is_ok());
    CHECK(dir.remove_file("moved"_str).is_err());
    CHECK(dir.remove_dir("moved"_str).is_ok());
    auto raw     = rstd::move(dir.try_clone().unwrap()).into_raw_fd();
    auto adopted = Dir::from_raw_fd(raw);
    CHECK(adopted.as_fd().as_raw_fd() == raw);
    CHECK(adopted.self_metadata().unwrap().ino() == dir.self_metadata().unwrap().ino());
    puts("fs::Dir windows paths, options, links, handles and mutations passed");
#else
    auto unsupported = rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::Unsupported };
    CHECK(Dir::open("."_str).unwrap_err().kind() == unsupported);
    CHECK(Dir::open_with("."_str, OpenOptions::make().read(true)).unwrap_err().kind() ==
          unsupported);
    CHECK(Dir::open_for_traversal("."_str).unwrap_err().kind() == unsupported);
    puts("fs::Dir unsupported backend checks passed");
#endif
    return 0;
}
