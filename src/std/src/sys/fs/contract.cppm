export module rstd:sys.fs.contract;
import :fs.types;
import :io;
import :time;
import rstd.alloc;

using ::alloc::vec::Vec;

namespace rstd::sys::fs
{

export struct OpenOptionsData {
    bool read;
    bool write;
    bool append;
    bool truncate;
    bool create;
    bool create_new;
    i32  custom_flags;
    u32  mode;
};

export struct FileTimesData {
    Option<rstd::time::SystemTime> const& accessed;
    Option<rstd::time::SystemTime> const& modified;
};

export struct MetadataData {
    rstd::fs::FileTypeKind         file_type { rstd::fs::FileTypeKind::Unknown };
    u64                            len {};
    u32                            permissions {};
    Option<rstd::time::SystemTime> accessed {};
    Option<rstd::time::SystemTime> modified {};
    Option<rstd::time::SystemTime> created {};
    u64                            dev {};
    u32                            rdev_major {};
    u32                            rdev_minor {};
    u64                            ino {};
    u32                            mode {};
    u64                            nlink {};
    u32                            uid {};
    u32                            gid {};
};

export struct DirectoryEntryData {
    Vec<u8>                name;
    rstd::fs::FileTypeKind file_type;
};

export enum class LockMode {
    Exclusive,
    Shared,
    TryExclusive,
    TryShared,
    Unlock,
};

} // namespace rstd::sys::fs
