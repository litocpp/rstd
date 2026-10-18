module;
#include <rstd/macro.hpp>
export module rstd.io:stdio;
export import :traits;

using namespace rstd::prelude;

struct ScopedLock {
private:
    int fd_;

public:
    explicit ScopedLock(int fd);
    ~ScopedLock() noexcept;
    ScopedLock(const ScopedLock&) = delete;
};

namespace rstd::io
{

inline constexpr char NEWLINE[] = { '\n' };

// ── Stdout ────────────────────────────────────────────────────────────────

/// A locked reference to the stdout stream, providing exclusive write access.
export struct StdoutLock {
    ScopedLock guard { 1 };
};

/// Handle to the standard output stream.
/// Thread-safe: concurrent writes are serialised by an internal mutex.
export struct Stdout {
    USE_TRAIT(Stdout)

    /// Acquire a lock and return a handle whose writes are atomic.
    auto lock() -> StdoutLock { return {}; }
};

/// Constructs a new handle to the standard output of the current process.
export inline auto stdout() noexcept -> Stdout {
    return {};
}

// ── Stderr ────────────────────────────────────────────────────────────────

/// Handle to the standard error stream.
/// Intentionally lock-free so it is usable from panic handlers.
export struct Stderr {
    auto is_terminal() const noexcept -> bool;
};

/// Constructs a new handle to the standard error of the current process.
export inline auto stderr() noexcept -> Stderr {
    return {};
}

// ── Stdin ─────────────────────────────────────────────────────────────────

/// A locked reference to the stdin stream, providing exclusive read access.
export struct StdinLock {
    ScopedLock guard { 0 };
};

/// Handle to the standard input stream.
export struct Stdin {
    USE_TRAIT(Stdin)

    /// Locks stdin and returns a guard providing exclusive read access.
    auto lock() -> StdinLock { return {}; }
};

/// Constructs a new handle to the standard input of the current process.
export inline auto stdin() noexcept -> Stdin {
    return {};
}

// ── Free formatting functions ─────────────────────────────────────────────

/// Format `args` and write to stdout (with lock).
export auto print_fmt(fmt::Arguments args) -> Result<empty>;

/// Format `args` and write to stderr (no lock — panic-safe).
export auto eprint_fmt(fmt::Arguments args) -> Result<empty>;

// ── print / println / eprint / eprintln ──────────────────────────────────
// Rust-style call-syntax structs using CTAD deduction guides.

/// Prints formatted text to stdout.
/// \tparam Args The types of format arguments.
export template<typename... Args>
struct print {
    print(fmt::format_string<Args...> fmt_str, Args&&... args) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        print_fmt({ fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) });
    }
};
// Zero-arg specialisation avoids zero-length array.
template<>
struct print<> {
    print(fmt::format_string<> fmt_str) {
        print_fmt({ fmt_str.data(), fmt_str.size(), nullptr, 0 });
    }
};
template<typename... Args>
print(fmt::format_string<Args...>, Args&&...) -> print<Args...>;

/// Prints formatted text to stdout, followed by a newline.
/// \tparam Args The types of format arguments.
export template<typename... Args>
struct println {
    println(fmt::format_string<Args...> fmt_str, Args&&... args) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        print_fmt({ fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) });
        print_fmt({ NEWLINE, 1, nullptr, 0 });
    }
};
template<>
struct println<> {
    println() { print_fmt({ NEWLINE, 1, nullptr, 0 }); }
    explicit println(fmt::format_string<> fmt_str) {
        print_fmt({ fmt_str.data(), fmt_str.size(), nullptr, 0 });
        print_fmt({ NEWLINE, 1, nullptr, 0 });
    }
};
template<typename... Args>
println(fmt::format_string<Args...>, Args&&...) -> println<Args...>;

/// Prints formatted text to stderr.
/// \tparam Args The types of format arguments.
export template<typename... Args>
struct eprint {
    eprint(fmt::format_string<Args...> fmt_str, Args&&... args) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        eprint_fmt({ fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) });
    }
};
template<>
struct eprint<> {
    eprint(fmt::format_string<> fmt_str) {
        eprint_fmt({ fmt_str.data(), fmt_str.size(), nullptr, 0 });
    }
};
template<typename... Args>
eprint(fmt::format_string<Args...>, Args&&...) -> eprint<Args...>;

/// Prints formatted text to stderr, followed by a newline.
/// \tparam Args The types of format arguments.
export template<typename... Args>
struct eprintln {
    eprintln(fmt::format_string<Args...> fmt_str, Args&&... args) {
        fmt::Argument arg_array[] = { fmt::Argument::make(args)... };
        eprint_fmt({ fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) });
        eprint_fmt({ NEWLINE, 1, nullptr, 0 });
    }
};
template<>
struct eprintln<> {
    eprintln() { eprint_fmt({ NEWLINE, 1, nullptr, 0 }); }
    explicit eprintln(fmt::format_string<> fmt_str) {
        eprint_fmt({ fmt_str.data(), fmt_str.size(), nullptr, 0 });
        eprint_fmt({ NEWLINE, 1, nullptr, 0 });
    }
};
template<typename... Args>
eprintln(fmt::format_string<Args...>, Args&&...) -> eprintln<Args...>;

} // namespace rstd::io

// ── Impl<io::Write, Stdout/Stderr/StdoutLock/StdinLock> ──────────────────
namespace rstd
{

template<>
struct Impl<io::Write, io::Stdout> : ImplBase<io::Stdout> {
    auto write(slice<u8> buf) -> io::Result<usize>;
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

template<>
struct Impl<io::Write, io::StdoutLock> : ImplBase<io::StdoutLock> {
    auto write(slice<u8> buf) -> io::Result<usize>;
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

template<>
struct Impl<io::Write, io::Stderr> : ImplBase<io::Stderr> {
    auto write(slice<u8> buf) -> io::Result<usize>;
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

template<>
struct Impl<io::Read, io::Stdin> : DefaultInImpl<io::Read, io::Stdin> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize>;
    auto read_buf(io::BorrowedCursor buf) -> io::Result<empty>;
};

template<>
struct Impl<io::Read, io::StdinLock> : DefaultInImpl<io::Read, io::StdinLock> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize>;
    auto read_buf(io::BorrowedCursor buf) -> io::Result<empty>;
};

} // namespace rstd
