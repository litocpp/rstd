export module rstd.io:util;
export import :traits;
import rstd.alloc;

using rstd::mem::MaybeUninit;

using ::alloc::collections::TryReserveError;

namespace rstd::io
{

inline auto reserve_error(TryReserveError error) -> Error {
    return Error::from_kind(ErrorKind { error == TryReserveError::CapacityOverflow
                                            ? ErrorKind::InvalidInput
                                            : ErrorKind::OutOfMemory });
}

/// Default collection for readers with no owned-source optimization.
export template<typename R, typename A>
    requires Impled<R, Read> && Impled<A, alloc::Allocator>
auto default_read_to_end(R&                        reader,
                         ::alloc::vec::Vec<u8, A>& output,
                         Option<usize>             size_hint = None()) -> Result<usize> {
    const auto start = output.len();
    if (size_hint.is_some()) {
        auto reserved = output.try_reserve(*size_hint);
        if (reserved.is_err()) return Err(reserve_error(reserved.unwrap_err_unchecked()));
    }
    auto initialized_until = output.len();
    auto max_read =
        size_hint.is_some() ? rstd::cmp::max(*size_hint, DEFAULT_BUF_SIZE) : DEFAULT_BUF_SIZE;
    for (;;) {
        if (output.len() == output.capacity()) {
            auto reserved = output.try_reserve(usize(32));
            if (reserved.is_err()) return Err(reserve_error(reserved.unwrap_err_unchecked()));
            initialized_until = output.len();
        }
        const auto length      = output.len();
        auto       available   = output.capacity() - length;
        auto       initialized = initialized_until - length;
        auto       count  = initialized > usize(32) ? initialized : rstd::min(available, max_read);
        auto       buffer = BorrowedBuf::from_uninit(
            rstd::slice_::split_at_mut(output.spare_capacity_mut().as_uninit(), count)
                .template get<0>());
        buffer.set_init_unchecked(rstd::min(initialized, count));
        auto result       = as<Read>(reader).read_buf(buffer.unfilled());
        initialized_until = rstd::cmp::max(initialized_until, length + buffer.initialized_len());
        output.set_len_unchecked(length + buffer.len());
        if (result.is_err()) {
            auto error = result.unwrap_err_unchecked();
            if (error.kind() == ErrorKind { ErrorKind::Interrupted }) continue;
            return Err(rstd::move(error));
        }
        if (buffer.len() == usize()) return Ok(output.len() - start);
        if (size_hint.is_none()) {
            if (buffer.initialized_len() != count)
                max_read = usize::MAX;
            else if (buffer.len() == max_read)
                max_read = max_read.saturating_mul(usize(2));
        }
    }
}

/// Static collection customization. A is not part of the Read vtable.
export template<typename A>
struct ReadToEnd {
    using Trait                  = ReadToEnd;
    static constexpr bool direct = false;
    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = ReadToEnd;
        auto read_to_end(::alloc::vec::Vec<u8, A>& output) -> Result<usize> {
            return trait_call<0>(this, output);
        }
    };
    template<typename T>
    using Funcs = TraitFuncs<&T::read_to_end>;
};

/// Output must not alias the reader or any storage borrowed by it.
export template<typename R, typename A>
    requires Impled<R, Read> && Impled<A, alloc::Allocator>
auto read_to_end(R& reader, ::alloc::vec::Vec<u8, A>& output) -> Result<usize> {
    return as<ReadToEnd<A>>(reader).read_to_end(output);
}

// ── Empty ─────────────────────────────────────────────────────────────────
/// A reader that always returns EOF and a writer that discards all bytes.
export struct Empty {};

/// Creates a value that implements Read (always EOF), Write (discards), and Seek.
export inline auto empty_io() noexcept -> Empty {
    return {};
}

// ── Repeat ────────────────────────────────────────────────────────────────
/// A reader that infinitely yields one byte value.
export struct Repeat {
    u8 byte;
};

/// Creates a reader that infinitely yields the given byte.
/// \param byte The byte value to repeat.
export inline auto repeat(u8 byte) noexcept -> Repeat {
    return { byte };
}

// ── Sink ──────────────────────────────────────────────────────────────────
/// A writer that discards all bytes and never errors.
export struct Sink {};

/// Creates a writer that successfully consumes all data without storing it.
export inline auto sink() noexcept -> Sink {
    return {};
}

// ── copy ──────────────────────────────────────────────────────────────────
/// Copy all bytes from `reader` into `writer`.  Returns bytes copied.
export template<typename R, typename W>
    requires Impled<R, io::Read> && Impled<W, io::Write>
auto copy(R& reader, W& writer) -> Result<u64> {
    rstd::array<MaybeUninit<byte>, DEFAULT_BUF_SIZE.to_primitive()> storage;
    auto buffer = BorrowedBuf::from_uninit(storage.as_mut_slice());
    u64  total {};
    for (;;) {
        buffer.clear();
        auto result = as<Read>(reader).read_buf(buffer.unfilled());
        if (buffer.len() != usize()) {
            auto written = io::write_all(writer, buffer.filled());
            if (written.is_err()) return Err(written.unwrap_err_unchecked());
            auto updated = total.checked_add(u64(buffer.len().to_primitive()));
            if (updated.is_none())
                return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
            total = updated.unwrap_unchecked();
        }
        if (result.is_err()) {
            auto error = result.unwrap_err_unchecked();
            if (error.kind() == ErrorKind { ErrorKind::Interrupted }) continue;
            return Err(rstd::move(error));
        }
        if (buffer.len() == usize()) return Ok(total);
    }
}

} // namespace rstd::io

// ── Impl specialisations (must be in namespace rstd) ─────────────────────
namespace rstd
{

template<typename A, typename R>
    requires Impled<R, io::Read> && Impled<A, alloc::Allocator>
struct Impl<io::ReadToEnd<A>, R> : ImplBase<R> {
    auto read_to_end(::alloc::vec::Vec<u8, A>& output) -> io::Result<usize> {
        return io::default_read_to_end(this->self(), output);
    }
};

template<>
struct Impl<io::Read, io::Empty> : DefaultInImpl<io::Read, io::Empty> {
    auto read(mut_ref<u8[]>) -> io::Result<usize> { return Ok(usize()); }
    auto read_buf(io::BorrowedCursor) -> io::Result<empty> { return Ok(empty {}); }
};

template<>
struct Impl<io::BufRead, io::Empty> : ImplBase<io::Empty> {
    auto fill_buf() -> io::Result<slice<u8>> {
        return Ok(slice<u8>::from_raw_parts(nullptr, usize {}));
    }
    auto consume(usize) -> void {}
};

template<>
struct Impl<io::Write, io::Empty> : ImplBase<io::Empty> {
    auto write(slice<u8> buf) -> io::Result<usize> { return Ok(buf.len()); }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

template<>
struct Impl<io::Seek, io::Empty> : ImplBase<io::Empty> {
    auto seek(io::SeekFrom) -> io::Result<u64> { return Ok(u64 {}); }
};

template<>
struct Impl<io::Read, io::Repeat> : DefaultInImpl<io::Read, io::Repeat> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> {
        if (! buf.is_empty()) rstd::mem::memset(buf.as_raw_ptr(), this->self().byte, buf.len());
        return Ok(buf.len());
    }
    auto read_buf(io::BorrowedCursor buf) -> io::Result<empty> {
        auto count = buf.capacity();
        if (count != usize())
            rstd::mem::memset(buf.as_uninit().as_raw_ptr(), this->self().byte, count);
        (void)buf.advance_unchecked(count);
        return Ok(empty {});
    }
};

template<>
struct Impl<io::Write, io::Sink> : ImplBase<io::Sink> {
    auto write(slice<u8> buf) -> io::Result<usize> { return Ok(buf.len()); }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

} // namespace rstd
