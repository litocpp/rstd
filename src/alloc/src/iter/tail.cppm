export module rstd.alloc:iter.tail;
export import :iter.storage;
export import :collections.vec_deque;

using namespace rstd::prelude;

export namespace rstd::iter
{

template<typename I, bool Take>
class Tail : public DefaultInClass<Tail<I, Take>, Iterator> {
    using Stored = stored_item_t<typename I::Item>;
    I                                      source_;
    usize                                  count_;
    ::alloc::collections::VecDeque<Stored> buffer_;
    bool                                   done_ = false;

public:
    using Item                         = Stored;
    static constexpr bool PROVEN_FUSED = true;
    Tail(I source, usize count): source_(rstd::move(source)), count_(count) {}
    auto next() -> Option<Item> {
        if constexpr (Take) {
            if (! done_) {
                done_ = true;
                if (count_ == usize()) return None();
                for (auto item = source_.next(); item.is_some(); item = source_.next()) {
                    if (buffer_.len() == count_) (void)buffer_.pop_front();
                    buffer_.push_back(
                        store_item<typename I::Item>(rstd::forward<typename I::Item>(*item)));
                }
            }
            return buffer_.pop_front();
        } else {
            if (done_) return None();
            for (auto item = source_.next(); item.is_some(); item = source_.next()) {
                auto stored = store_item<typename I::Item>(rstd::forward<typename I::Item>(*item));
                if (buffer_.len() == count_) {
                    if (count_ == usize()) return Some(rstd::move(stored));
                    auto result = buffer_.pop_front();
                    buffer_.push_back(rstd::move(stored));
                    return result;
                }
                buffer_.push_back(rstd::move(stored));
            }
            done_ = true;
            buffer_.clear();
            return None();
        }
    }
    auto size_hint() const -> SizeHint {
        if (done_) return { buffer_.len(), Some(buffer_.len()) };
        return { usize(), Take ? Some<usize>(count_) : source_.size_hint().template get<1>() };
    }
};

template<typename S>
    requires(! mtp::is_ref<S>) && into_iterable<S>
auto take_last(S&& source, usize count) {
    return Tail<into_iter_t<S>, true>(iter::into_iter(rstd::forward<S>(source)), count);
}
template<typename S>
    requires(! mtp::is_ref<S>) && into_iterable<S>
auto skip_last(S&& source, usize count) {
    return Tail<into_iter_t<S>, false>(iter::into_iter(rstd::forward<S>(source)), count);
}

} // namespace rstd::iter
