module;
#include <rstd/macro.hpp>
export module rstd:io.cursor;
export import :io.traits;
import rstd.alloc;
import :forward;

using rstd_alloc::vec::Vec;

namespace rstd::io
{

// ── Cursor ────────────────────────────────────────────────────────────────
/// In-memory reader/writer with a position cursor.
/// Supported inner types: `Vec<u8>` (R+W+Seek+BufRead), `slice<u8>` (R+Seek+BufRead).
export template<typename T>
class Cursor {
    T   inner_;
    u64 pos_ {};

public:
    USE_TRAIT(Cursor)

    explicit Cursor(T inner) noexcept: inner_(rstd::move(inner)), pos_() {}

    /// Returns a reference to the underlying value in this cursor.
    auto get_ref() const noexcept -> const T& { return inner_; }
    /// Returns a mutable reference to the underlying value in this cursor.
    auto get_mut() noexcept -> T& { return inner_; }
    /// Returns the current position of this cursor.
    auto position() const noexcept -> u64 { return pos_; }
    /// Sets the position of this cursor.
    /// \param p The new byte offset from the start.
    void set_position(u64 p) noexcept { pos_ = p; }
    /// Consumes this cursor, returning the underlying value.
    auto into_inner() -> T { return rstd::move(inner_); }
};

} // namespace rstd::io

using namespace rstd::prelude;

inline auto cursor_data(const Vec<u8>& v) noexcept -> const byte* {
    return v.begin();
}
inline auto cursor_len(const Vec<u8>& v) noexcept -> usize {
    return v.len();
}
inline auto cursor_data(slice<u8> s) noexcept -> const byte* {
    return s.as_raw_ptr();
}
inline auto cursor_len(slice<u8> s) noexcept -> usize {
    return s.len();
}

// ── Impl specialisations ──────────────────────────────────────────────────
namespace rstd
{

// Read — Vec<u8> and slice<u8>
template<typename T>
    requires(mtp::same_as<T, Vec<u8>> || mtp::same_as<T, slice<u8>>)
struct Impl<io::Read, io::Cursor<T>> : ImplBase<io::Cursor<T>> {
    auto read(mut_ref<u8[]> buf) -> io::Result<usize> {
        auto&      self         = this->self();
        auto const total        = cursor_len(self.inner_).to_primitive();
        auto const raw_position = self.pos_.to_primitive();
        auto const position =
            raw_position >= total ? total : static_cast<rstd::size_t>(raw_position);
        auto const count = rstd::min(buf.len().to_primitive(), total - position);
        if (count == 0) return Ok(usize {});
        auto source = slice<u8>::from_raw_parts(cursor_data(self.inner_) + position, usize(count));
        rstd::mem::memcpy(buf.as_raw_ptr(), source.as_raw_ptr(), usize(count));
        self.pos_ = u64(raw_position + static_cast<rstd::uint64_t>(count));
        return Ok(usize(count));
    }
};

// BufRead — Vec<u8> and slice<u8>
template<typename T>
    requires(mtp::same_as<T, Vec<u8>> || mtp::same_as<T, slice<u8>>)
struct Impl<io::BufRead, io::Cursor<T>> : ImplBase<io::Cursor<T>> {
    auto fill_buf() -> io::Result<slice<u8>> {
        auto&      self         = this->self();
        auto const total        = cursor_len(self.inner_).to_primitive();
        auto const raw_position = self.pos_.to_primitive();
        auto const position =
            raw_position >= total ? total : static_cast<rstd::size_t>(raw_position);
        if (position == total) return Ok(slice<u8> {});
        return Ok(slice<u8>::from_raw_parts(cursor_data(self.inner_) + position,
                                            usize(total - position)));
    }
    auto consume(usize amt) -> void {
        auto&      self         = this->self();
        auto const total        = cursor_len(self.inner_).to_primitive();
        auto const raw_position = self.pos_.to_primitive();
        auto const position =
            raw_position >= total ? total : static_cast<rstd::size_t>(raw_position);
        auto const consumed = rstd::min(amt.to_primitive(), total - position);
        self.pos_           = u64(position + consumed);
    }
};

// Seek — Vec<u8> and slice<u8>
template<typename T>
    requires(mtp::same_as<T, Vec<u8>> || mtp::same_as<T, slice<u8>>)
struct Impl<io::Seek, io::Cursor<T>> : ImplBase<io::Cursor<T>> {
    auto seek(io::SeekFrom sf) -> io::Result<u64> {
        auto& self = this->self();
        if (sf.which == io::SeekFrom::Which::Start) {
            self.pos_ = sf.start;
            return Ok(self.pos_);
        }

        rstd::int128_t base = 0;
        switch (sf.which) {
        case io::SeekFrom::Which::Start: break;
        case io::SeekFrom::Which::End:
            base = static_cast<rstd::int128_t>(cursor_len(self.inner_).to_primitive());
            break;
        case io::SeekFrom::Which::Current:
            base = static_cast<rstd::int128_t>(self.pos_.to_primitive());
            break;
        }
        auto const new_position = base + static_cast<rstd::int128_t>(sf.offset.to_primitive());
        if (new_position < 0 || new_position > static_cast<rstd::int128_t>(~rstd::uint64_t(0))) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        }
        self.pos_ = u64(new_position);
        return Ok(self.pos_);
    }
};

// Write — Vec<u8> only (grows the buffer)
template<>
struct Impl<io::Write, io::Cursor<Vec<u8>>> : ImplBase<io::Cursor<Vec<u8>>> {
    auto write(slice<u8> buf) -> io::Result<usize> {
        auto&      self         = this->self();
        auto const raw_position = self.pos_.to_primitive();
        if (raw_position > ~rstd::size_t(0)) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        }
        auto const position = static_cast<rstd::size_t>(raw_position);
        auto const count    = buf.len().to_primitive();
        if (count > ~rstd::size_t(0) - position) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        }
        auto const end = position + count;
        for (rstd::size_t i = self.inner_.len().to_primitive(); i < end; ++i) {
            self.inner_.push(u8 {});
        }
        if (count != 0) {
            auto destination =
                mut_ref<u8[]>::from_raw_parts(self.inner_.begin() + position, usize(count));
            rstd::mem::memcpy(destination.as_raw_ptr(), buf.as_raw_ptr(), buf.len());
        }
        self.pos_ = u64(end);
        return Ok(buf.len());
    }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

} // namespace rstd
