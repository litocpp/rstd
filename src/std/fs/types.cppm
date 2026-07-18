export module rstd:fs.types;
import rstd.core;

export namespace rstd::fs
{

enum class FileTypeKind : u8
{
    Unknown,
    File,
    Directory,
    Symlink,
    Fifo,
    BlockDevice,
    CharDevice,
    Socket,
};

} // namespace rstd::fs
