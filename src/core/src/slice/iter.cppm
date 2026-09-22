export module rstd.core:slice.iter;
export import :slice;
export import :iter.traits;
import :panicking;

template<typename T>
constexpr auto slice_part(rstd::slice<T> source, rstd::usize start, rstd::usize length)
    -> rstd::slice<T> {
    auto* data = source.as_raw_ptr();
    if (start != rstd::usize()) data += start.to_primitive();
    return rstd::slice<T>::from_raw_parts(data, length);
}

export namespace rstd::slice_
{

template<typename T, bool Exact>
class SliceChunks : public DefaultInClass<SliceChunks<T, Exact>, iter::Iterator> {
    slice<T> source_;
    usize    width_;
    usize    front_;
    usize    back_;
    slice<T> remainder_;

public:
    using Item                                = slice<T>;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;

    constexpr SliceChunks(slice<T> source [[clang::lifetimebound]], usize width)
        : source_(source), width_(width), front_(), back_(source.len()), remainder_() {
        if (width == usize()) rstd::panic("chunks called with width 0");
        if constexpr (Exact) {
            auto tail = source.len() % width;
            back_ -= tail;
            remainder_ = slice_part(source, back_, tail);
        }
    }

    constexpr auto len() const -> usize {
        auto left = back_ - front_;
        return left / width_ + usize(left % width_ != usize());
    }
    constexpr auto size_hint() const -> iter::SizeHint { return { len(), Some(len()) }; }
    constexpr auto next() -> Option<Item> {
        if (front_ == back_) return None();
        auto width  = back_ - front_ < width_ ? back_ - front_ : width_;
        auto result = slice_part(source_, front_, width);
        front_ += width;
        return Some(result);
    }
    constexpr auto next_back() -> Option<Item> {
        if (front_ == back_) return None();
        auto width = (back_ - front_) % width_;
        if (width == usize()) width = width_;
        back_ -= width;
        return Some(slice_part(source_, back_, width));
    }
    constexpr auto remainder() const -> slice<T>
        requires Exact
    {
        return remainder_;
    }
};

template<typename T>
using Chunks = SliceChunks<T, false>;
template<typename T>
using ChunksExact = SliceChunks<T, true>;

template<typename T>
class Windows : public DefaultInClass<Windows<T>, iter::Iterator> {
    slice<T> source_;
    usize    width_;
    usize    front_;
    usize    back_;

public:
    using Item                                = slice<T>;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;

    constexpr Windows(slice<T> source [[clang::lifetimebound]], usize width)
        : source_(source), width_(width), front_(), back_() {
        if (width == usize()) rstd::panic("windows called with width 0");
        if (width <= source.len()) back_ = source.len() - width + usize(1);
    }
    constexpr auto len() const -> usize { return back_ - front_; }
    constexpr auto size_hint() const -> iter::SizeHint { return { len(), Some(len()) }; }
    constexpr auto next() -> Option<Item> {
        if (front_ == back_) return None();
        return Some(slice_part(source_, front_++, width_));
    }
    constexpr auto next_back() -> Option<Item> {
        if (front_ == back_) return None();
        return Some(slice_part(source_, --back_, width_));
    }
};

template<typename T, typename P>
class ChunkBy : public DefaultInClass<ChunkBy<T, P>, iter::Iterator> {
    slice<T> source_;
    P        predicate_;
    usize    front_;
    usize    back_;

public:
    using Item                                = slice<T>;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_FUSED        = true;

    constexpr ChunkBy(slice<T> source [[clang::lifetimebound]], P predicate)
        : source_(source), predicate_(rstd::move(predicate)), front_(), back_(source.len()) {}
    constexpr auto size_hint() const -> iter::SizeHint {
        return { usize(front_ != back_), Some(back_ - front_) };
    }
    constexpr auto next() -> Option<Item> {
        if (front_ == back_) return None();
        auto end = front_ + usize(1);
        while (end < back_ && predicate_(source_[end - usize(1)], source_[end])) ++end;
        auto result = slice_part(source_, front_, end - front_);
        front_      = end;
        return Some(result);
    }
    constexpr auto next_back() -> Option<Item> {
        if (front_ == back_) return None();
        auto start = back_ - usize(1);
        while (start > front_ && predicate_(source_[start - usize(1)], source_[start])) --start;
        auto result = slice_part(source_, start, back_ - start);
        back_       = start;
        return Some(result);
    }
};

template<typename T>
constexpr auto chunks(slice<T> source [[clang::lifetimebound]], usize width) -> Chunks<T> {
    return Chunks<T>(source, width);
}
template<typename T>
constexpr auto chunks_exact(slice<T> source [[clang::lifetimebound]], usize width)
    -> ChunksExact<T> {
    return ChunksExact<T>(source, width);
}
template<typename T>
constexpr auto windows(slice<T> source [[clang::lifetimebound]], usize width) -> Windows<T> {
    return Windows<T>(source, width);
}
template<typename T, typename P>
constexpr auto chunk_by(slice<T> source [[clang::lifetimebound]], P predicate) -> ChunkBy<T, P> {
    return ChunkBy<T, P>(source, rstd::move(predicate));
}

} // namespace rstd::slice_
