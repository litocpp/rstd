export module rstd.io:error;
export import rstd.core;

namespace rstd::io::error
{

/// A list specifying general categories of I/O error.
///
/// This list is intended to grow over time and it is not recommended to
/// exhaustively match against it.
struct ErrorKind {
    enum Entity
    {
        /// An entity was not found, often a file.
        NotFound,
        /// The operation lacked the necessary privileges to complete.
        PermissionDenied,
        /// The connection was refused by the remote server.
        ConnectionRefused,
        /// The connection was reset by the remote server.
        ConnectionReset,
        /// The remote host is not reachable.
        HostUnreachable,
        /// The network containing the remote host is not reachable.
        NetworkUnreachable,
        /// The connection was aborted (terminated) by the remote server.
        ConnectionAborted,
        /// The network operation failed because it was not connected yet.
        NotConnected,
        /// A socket address could not be bound because the address is already in
        /// use elsewhere.
        AddrInUse,
        /// A nonexistent interface was requested or the requested address was not
        /// local.
        AddrNotAvailable,
        /// The system's networking is down.
        NetworkDown,
        /// The operation failed because a pipe was closed.
        BrokenPipe,
        /// An entity already exists, often a file.
        AlreadyExists,
        /// The operation needs to block to complete, but the blocking operation was
        /// requested to not occur.
        WouldBlock,
        /// A filesystem object is, unexpectedly, not a directory.
        ///
        /// For example, a filesystem path was specified where one of the intermediate directory
        /// components was, in fact, a plain file.
        NotADirectory,
        /// The filesystem object is, unexpectedly, a directory.
        ///
        /// A directory was specified when a non-directory was expected.
        IsADirectory,
        /// A non-empty directory was specified where an empty directory was expected.
        DirectoryNotEmpty,
        /// The filesystem or storage medium is read-only, but a write operation was attempted.
        ReadOnlyFilesystem,
        /// Loop in the filesystem or IO subsystem; often, too many levels of symbolic links.
        ///
        /// There was a loop (or excessively long chain) resolving a filesystem object
        /// or file IO object.
        ///
        /// On Unix this is usually the result of a symbolic link loop; or, of exceeding the
        /// system-specific limit on the depth of symlink traversal.
        FilesystemLoop,
        /// Stale network file handle.
        ///
        /// With some network filesystems, notably NFS, an open file (or directory) can be
        /// invalidated by problems with the network or server.
        StaleNetworkFileHandle,
        /// A parameter was incorrect.
        InvalidInput,
        /// Data not valid for the operation were encountered.
        ///
        /// Unlike [`InvalidInput`], this typically means that the operation
        /// parameters were valid, however the error was caused by malformed
        /// input data.
        ///
        /// For example, a function that reads a file into a string will error with
        /// `InvalidData` if the file's contents are not valid UTF-8.
        ///
        /// [`InvalidInput`]: ErrorKind::InvalidInput
        InvalidData,
        /// The I/O operation's timeout expired, causing it to be canceled.
        TimedOut,
        /// An error returned when an operation could not be completed because a
        /// call to [`write`] returned [`Ok(0)`].
        ///
        /// This typically means that an operation could only succeed if it wrote a
        /// particular number of bytes but only a smaller number of bytes could be
        /// written.
        ///
        /// [`write`]: crate::io::Write::write
        /// [`Ok(0)`]: Ok
        WriteZero,
        /// The underlying storage (typically, a filesystem) is full.
        ///
        /// This does not include out of quota errors.
        StorageFull,
        /// Seek on unseekable file.
        ///
        /// Seeking was attempted on an open file handle which is not suitable for seeking - for
        /// example, on Unix, a named pipe opened with `File::open`.
        NotSeekable,
        /// Filesystem quota or some other kind of quota was exceeded.
        QuotaExceeded,
        /// File larger than allowed or supported.
        ///
        /// This might arise from a hard limit of the underlying filesystem or file access API, or
        /// from an administratively imposed resource limitation.  Simple disk full, and out of
        /// quota, have their own errors.
        FileTooLarge,
        /// Resource is busy.
        ResourceBusy,
        /// Executable file is busy.
        ///
        /// An attempt was made to write to a file which is also in use as a running program.  (Not
        /// all operating systems detect this situation.)
        ExecutableFileBusy,
        /// Deadlock (avoided).
        ///
        /// A file locking operation would result in deadlock.  This situation is typically
        /// detected, if at all, on a best-effort basis.
        Deadlock,
        /// Cross-device or cross-filesystem (hard) link or rename.
        CrossesDevices,
        /// Too many (hard) links to the same filesystem object.
        ///
        /// The filesystem does not support making so many hardlinks to the same file.
        TooManyLinks,
        /// A filename was invalid.
        ///
        /// This error can also occur if a length limit for a name was exceeded.
        InvalidFilename,
        /// Program argument list too long.
        ///
        /// When trying to run an external program, a system or process limit on the size of the
        /// arguments would have been exceeded.
        ArgumentListTooLong,
        /// This operation was interrupted.
        ///
        /// Interrupted operations can typically be retried.
        Interrupted,
        /// This operation is unsupported on this platform.
        ///
        /// This means that the operation can never succeed.
        Unsupported,
        // ErrorKinds which are primarily categorisations for OS error
        // codes should be added above.
        //
        /// An error returned when an operation could not be completed because an
        /// "end of file" was reached prematurely.
        ///
        /// This typically means that an operation could only succeed if it read a
        /// particular number of bytes but only a smaller number of bytes could be
        /// read.
        UnexpectedEof,

        /// An operation could not be completed, because it failed
        /// to allocate enough memory.
        OutOfMemory,

        /// The operation was partially successful and needs to be checked
        /// later on due to not blocking.
        InProgress,

        // "Unusual" error kinds which do not correspond simply to (sets
        // of) OS error codes, should be added just above this comment.
        // `Other` and `Uncategorized` should remain at the end:
        //
        /// A custom error that does not fall under any other I/O error kind.
        ///
        /// This can be used to construct your own [`Error`]s that do not match any
        /// [`ErrorKind`].
        ///
        /// This [`ErrorKind`] is not used by the standard library.
        ///
        /// Errors from the standard library that do not fall under any of the I/O
        /// error kinds cannot be `match`ed on, and will only match a wildcard (`_`) pattern.
        /// New [`ErrorKind`]s might be added in the future for some of those.
        Other,
        /// Any I/O error from the standard library that's not part of this list.
        ///
        /// Errors that are `Uncategorized` now may move to a different or a new
        /// [`ErrorKind`] variant in the future. It is not recommended to match
        /// an error against `Uncategorized`; use a wildcard match (`_`) instead.
        Uncategorized,
    } code;

    using Self = ErrorKind;

    auto as_str(this Self const& self) -> ref<str> {
        switch (self.code) {
        case AddrInUse: return "address in use";
        case AddrNotAvailable: return "address not available";
        case AlreadyExists: return "entity already exists";
        case ArgumentListTooLong: return "argument list too long";
        case BrokenPipe: return "broken pipe";
        case ConnectionAborted: return "connection aborted";
        case ConnectionRefused: return "connection refused";
        case ConnectionReset: return "connection reset";
        case CrossesDevices: return "cross-device link or rename";
        case Deadlock: return "deadlock";
        case DirectoryNotEmpty: return "directory not empty";
        case ExecutableFileBusy: return "executable file busy";
        case FileTooLarge: return "file too large";
        case FilesystemLoop: return "filesystem loop or indirection limit (e.g. symlink loop)";
        case HostUnreachable: return "host unreachable";
        case InProgress: return "in progress";
        case Interrupted: return "operation interrupted";
        case InvalidData: return "invalid data";
        case InvalidFilename: return "invalid filename";
        case InvalidInput: return "invalid input parameter";
        case IsADirectory: return "is a directory";
        case NetworkDown: return "network down";
        case NetworkUnreachable: return "network unreachable";
        case NotADirectory: return "not a directory";
        case NotConnected: return "not connected";
        case NotFound: return "entity not found";
        case NotSeekable: return "seek on unseekable file";
        case Other: return "other error";
        case OutOfMemory: return "out of memory";
        case PermissionDenied: return "permission denied";
        case QuotaExceeded: return "quota exceeded";
        case ReadOnlyFilesystem: return "read-only filesystem or storage medium";
        case ResourceBusy: return "resource busy";
        case StaleNetworkFileHandle: return "stale network file handle";
        case StorageFull: return "no storage space";
        case TimedOut: return "timed out";
        case TooManyLinks: return "too many links";
        case Uncategorized: return "uncategorized error";
        case UnexpectedEof: return "unexpected end of file";
        case Unsupported: return "unsupported";
        case WouldBlock: return "operation would block";
        case WriteZero: return "write zero";
        }
        return "";
    }
};

struct Error {
    u64 repr;

    using enum ErrorKind::Entity;
};

template<typename T>
using Result = result::Result<T, Error>;

} // namespace rstd::io::error
