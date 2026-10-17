module rstd.io;
import :stdio;
import rstd.sys.io.stdio;
import rstd.sys.sync.mutex;

using namespace rstd::prelude;

// Global mutexes — inline avoids local-static guards (__cxa_guard_*) which
// are unavailable in -nostdlib++ builds.  Mutex is trivially destructible.
inline rstd::sys::sync::mutex::Mutex g_stdout_mutex = rstd::sys::sync::mutex::Mutex::make();
inline rstd::sys::sync::mutex::Mutex g_stdin_mutex  = rstd::sys::sync::mutex::Mutex::make();

inline auto& stdout_mutex() noexcept {
    return g_stdout_mutex;
}
inline auto& stdin_mutex() noexcept {
    return g_stdin_mutex;
}

ScopedLock::ScopedLock(int fd): fd_(fd) {
    (fd_ == 1 ? stdout_mutex() : stdin_mutex()).lock();
}

ScopedLock::~ScopedLock() noexcept {
    (fd_ == 1 ? stdout_mutex() : stdin_mutex()).unlock();
}

// fmt::Write adapter that writes directly to a file descriptor.
// Used to avoid a String heap allocation inside print_fmt/eprint_fmt.
struct FdWriter {
    int                     fd;
    rstd::io::Result<empty> result { rstd::Ok(empty {}) };
};

namespace rstd
{
template<>
struct Impl<fmt::Write, FdWriter> : ImplBase<FdWriter> {
    auto write_str(ref<str> const& value) -> bool {
        auto& self = this->self();
        if (self.result.is_err()) return false;
        auto* p   = value.data();
        auto  len = value.size().to_primitive();
        while (len != 0) {
            auto bytes = slice<byte>::from_raw_parts(p, usize(len));
            auto res   = sys::io::stdio::write_fd(self.fd, bytes);
            if (res.is_err()) {
                self.result = Err(res.unwrap_err_unchecked());
                return false;
            }
            auto const written = res.unwrap_unchecked().to_primitive();
            if (written == 0) {
                self.result = Err(io::error::Error_WRITE_ALL_EOF);
                return false;
            }
            if (written > len) {
                self.result = Err(io::error::Error::from_kind(
                    io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
                return false;
            }
            p += written;
            len -= written;
        }
        return true;
    }
};
} // namespace rstd

namespace rstd::io
{
auto print_fmt(fmt::Arguments args) -> Result<empty> {
    ScopedLock     guard(1);
    FdWriter       w { 1 };
    fmt::Formatter f(w);
    f.write_fmt(args);
    return rstd::move(w.result);
}

auto eprint_fmt(fmt::Arguments args) -> Result<empty> {
    FdWriter       w { 2 };
    fmt::Formatter f(w);
    f.write_fmt(args);
    return rstd::move(w.result);
}

auto Stderr::is_terminal() const noexcept -> bool {
    return sys::io::stdio::is_terminal_fd(2);
}
} // namespace rstd::io

namespace rstd
{
auto Impl<io::Write, io::Stdout>::write(slice<u8> buf) -> io::Result<usize> {
    ScopedLock guard(1);
    return sys::io::stdio::write_fd(1, as_bytes(buf));
}

auto Impl<io::Write, io::StdoutLock>::write(slice<u8> buf) -> io::Result<usize> {
    // Mutex is already held by the StdoutLock.
    return sys::io::stdio::write_fd(1, as_bytes(buf));
}

auto Impl<io::Write, io::Stderr>::write(slice<u8> buf) -> io::Result<usize> {
    return sys::io::stdio::write_fd(2, as_bytes(buf));
}

auto Impl<io::Read, io::Stdin>::read(mut_ref<u8[]> buf) -> io::Result<usize> {
    ScopedLock guard(0);
    return sys::io::stdio::read_fd(0, as_bytes_mut(buf));
}

auto Impl<io::Read, io::StdinLock>::read(mut_ref<u8[]> buf) -> io::Result<usize> {
    return sys::io::stdio::read_fd(0, as_bytes_mut(buf));
}
} // namespace rstd
