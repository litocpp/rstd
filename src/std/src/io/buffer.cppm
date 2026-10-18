export module rstd.io:buffer;
export import rstd.core;

using rstd::mem::MaybeUninit;

export namespace rstd::io
{
class BorrowedCursor;

/// Tracks filled <= initialized <= capacity without owning storage.
/// Only the filled prefix is exposed as readable input data. Moving or resizing the owner
/// invalidates this borrow and its cursors; no other writer may invalidate initialized bytes.
class BorrowedBuf {
    mut_ref<MaybeUninit<byte>[]> storage_;
    usize                        filled_ {};
    usize                        initialized_ {};
    explicit BorrowedBuf(mut_ref<MaybeUninit<byte>[]> storage, usize initialized)
        : storage_(storage), initialized_(initialized) {}
    friend class BorrowedCursor;

public:
    BorrowedBuf(const BorrowedBuf&)                      = delete;
    auto operator=(const BorrowedBuf&) -> BorrowedBuf&   = delete;
    BorrowedBuf(BorrowedBuf&&)                           = delete;
    auto        operator=(BorrowedBuf&&) -> BorrowedBuf& = delete;
    static auto from_initialized(mut_ref<u8[]> bytes) -> BorrowedBuf {
        return BorrowedBuf(
            mut_ref<MaybeUninit<byte>[]>::from_bytes(bytes.as_byte_ptr(), bytes.len()),
            bytes.len());
    }
    /// Borrows byte slots without reading or initializing their contents.
    static auto from_uninit(mut_ref<MaybeUninit<byte>[]> storage) -> BorrowedBuf {
        return BorrowedBuf(storage, usize());
    }
    auto capacity() const noexcept -> usize { return storage_.len(); }
    auto len() const noexcept -> usize { return filled_; }
    auto initialized_len() const noexcept -> usize { return initialized_; }
    auto filled() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        return slice<u8>::from_raw_parts(reinterpret_cast<const byte*>(storage_.as_raw_ptr()),
                                         filled_);
    }
    /// The first count bytes must already be initialized, even if not filled with input data.
    void set_init_unchecked(usize count) {
        if (count > capacity()) rstd::panic { "initialized byte count exceeds capacity" };
        if (count > initialized_) initialized_ = count;
    }
    /// Invalidates existing cursors while retaining initialization knowledge.
    void clear() noexcept { filled_ = usize(); }
    auto unfilled() & noexcept [[clang::lifetimebound]] -> BorrowedCursor;
};

/// An exclusive, non-owning cursor. Do not use a parent while a reborrow is active.
class BorrowedCursor {
    BorrowedBuf* buffer_;
    usize        end_;
    BorrowedCursor(BorrowedBuf& buffer, usize end): buffer_(&buffer), end_(end) {}
    friend class BorrowedBuf;

public:
    BorrowedCursor(const BorrowedCursor&)                    = delete;
    auto operator=(const BorrowedCursor&) -> BorrowedCursor& = delete;
    BorrowedCursor(BorrowedCursor&& other) noexcept
        : buffer_(rstd::exchange(other.buffer_, nullptr)), end_(other.end_) {}
    auto operator=(BorrowedCursor&&) -> BorrowedCursor& = delete;
    auto capacity() const noexcept -> usize { return end_ - buffer_->filled_; }
    auto initialized_len() const noexcept -> usize {
        return rstd::min(buffer_->initialized_, end_) - buffer_->filled_;
    }
    auto written() const noexcept -> usize { return buffer_->filled_; }
    auto reborrow() & noexcept [[clang::lifetimebound]] -> BorrowedCursor {
        return BorrowedCursor(*buffer_, end_);
    }
    auto limit(usize count) & noexcept [[clang::lifetimebound]] -> BorrowedCursor {
        return BorrowedCursor(*buffer_, buffer_->filled_ + rstd::min(count, capacity()));
    }
    /// Borrows the unfilled slots. Initialize them before reading or advancing the cursor.
    /// Native backends may write through as_raw_ptr() and report progress with advance_unchecked().
    auto as_uninit() const noexcept [[clang::lifetimebound]] -> mut_ref<MaybeUninit<byte>[]> {
        auto pointer = buffer_->storage_.as_byte_ptr();
        if (buffer_->filled_ != usize()) pointer += buffer_->filled_.to_primitive();
        return mut_ref<MaybeUninit<byte>[]>::from_bytes(pointer, capacity());
    }
    auto ensure_init() [[clang::lifetimebound]] -> mut_ref<u8[]> {
        if (buffer_->initialized_ < end_) {
            auto pointer = buffer_->storage_.as_byte_ptr() + buffer_->initialized_.to_primitive();
            mem::memset(pointer, u8(), end_ - buffer_->initialized_);
            buffer_->initialized_ = end_;
        }
        auto bytes = as_uninit();
        return mut_ref<u8[]>::from_raw_parts(reinterpret_cast<byte*>(bytes.as_raw_ptr()),
                                             bytes.len());
    }
    /// Advances over initialized bytes; returns false without changing progress on overflow.
    auto advance(usize count) noexcept -> bool {
        if (count > capacity() || count > buffer_->initialized_ - buffer_->filled_) return false;
        buffer_->filled_ += count;
        return true;
    }
    /// The backend must have initialized count bytes. Only the bounds are checked here.
    auto advance_unchecked(usize count) noexcept -> bool {
        if (count > capacity()) return false;
        buffer_->filled_ += count;
        if (buffer_->filled_ > buffer_->initialized_) buffer_->initialized_ = buffer_->filled_;
        return true;
    }
    void append(slice<u8> bytes) {
        if (bytes.len() > capacity()) rstd::panic { "borrowed buffer capacity exceeded" };
        if (! bytes.is_empty())
            mem::memcpy(as_uninit().as_raw_ptr(), bytes.as_raw_ptr(), bytes.len());
        (void)advance_unchecked(bytes.len());
    }
};
inline auto BorrowedBuf::unfilled() & noexcept -> BorrowedCursor {
    return BorrowedCursor(*this, capacity());
}
} // namespace rstd::io
