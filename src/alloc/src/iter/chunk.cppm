export module rstd.alloc:iter.chunk;
export import :iter.storage;

using namespace rstd::prelude;
using ::alloc::vec::Vec;

export namespace rstd::iter
{

template<typename I>
class Chunks : public DefaultInClass<Chunks<I>, Iterator> {
    I     source_;
    usize width_;
    bool  done_ = false;

public:
    using Item                         = Vec<stored_item_t<typename I::Item>>;
    static constexpr bool PROVEN_FUSED = true;
    Chunks(I source, usize width): source_(rstd::move(source)), width_(width) {
        if (width == usize()) rstd::panic("chunks called with width 0");
    }
    auto next() -> Option<Item> {
        if (done_) return None();
        Item block;
        while (block.len() < width_) {
            auto item = source_.next();
            if (item.is_none()) {
                done_ = true;
                break;
            }
            block.push(store_item<typename I::Item>(rstd::forward<typename I::Item>(*item)));
        }
        if (block.is_empty()) return None();
        return Some(rstd::move(block));
    }
    auto size_hint() const -> SizeHint {
        if (done_) return { usize(), Some(usize()) };
        auto ceil = [this](usize n) {
            return n / width_ + usize(n % width_ != usize());
        };
        auto hint  = source_.size_hint();
        auto upper = hint.template get<1>();
        if (upper.is_some()) upper = Some(ceil(*upper));
        return { ceil(hint.template get<0>()), rstd::move(upper) };
    }
};

template<typename S>
    requires(! mtp::is_ref<S>) && into_iterable<S>
auto chunks(S&& source, usize width) -> Chunks<into_iter_t<S>> {
    return Chunks<into_iter_t<S>>(iter::into_iter(rstd::forward<S>(source)), width);
}

} // namespace rstd::iter
