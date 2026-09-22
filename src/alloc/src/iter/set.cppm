export module rstd.alloc:iter.set;
export import :iter.unique;

using namespace rstd::prelude;

export namespace rstd::iter
{

template<typename I, typename F>
using query_key_t =
    decltype(mtp::declval<F&>()(mtp::declval<const mtp::rm_ref<typename I::Item>&>()));

template<typename L, typename R, typename LF, typename RF, typename S, typename Eq, int Mode>
class SetOperation : public DefaultInClass<SetOperation<L, R, LF, RF, S, Eq, Mode>, Iterator> {
    using Key = query_key_t<L, LF>;
    L                                         left_;
    R                                         right_;
    LF                                        left_key_;
    RF                                        right_key_;
    ::alloc::collections::HashSet<Key, S, Eq> keys_;
    bool                                      initialized_ = false;
    bool                                      done_        = false;

public:
    using Item                         = typename L::Item;
    static constexpr bool PROVEN_FUSED = true;
    SetOperation(L left, R right, LF left_key, RF right_key, S hasher, Eq equal)
        : left_(rstd::move(left)),
          right_(rstd::move(right)),
          left_key_(rstd::move(left_key)),
          right_key_(rstd::move(right_key)),
          keys_(
              ::alloc::collections::HashSet<Key, S, Eq>::with_hasher_and_equal(rstd::move(hasher),
                                                                               rstd::move(equal))) {
        static_assert(mtp::same_as<Key, query_key_t<R, RF>>);
        if constexpr (Mode == 0) static_assert(mtp::same_as<Item, typename R::Item>);
        check_persistent_key<Item, Key>();
        check_persistent_key<typename R::Item, Key>();
    }
    auto next() -> Option<Item> {
        if (done_) return None();
        if constexpr (Mode == 0) {
            if (! initialized_) {
                for (auto item = left_.next(); item.is_some(); item = left_.next()) {
                    const auto& observed = *item;
                    if (keys_.insert(left_key_(observed))) return item;
                }
                initialized_ = true;
            }
            for (auto item = right_.next(); item.is_some(); item = right_.next()) {
                const auto& observed = *item;
                if (keys_.insert(right_key_(observed))) return item;
            }
        } else {
            auto item = left_.next();
            if (item.is_none()) {
                done_ = true;
                return None();
            }
            if (! initialized_) {
                initialized_ = true;
                for (auto right = right_.next(); right.is_some(); right = right_.next()) {
                    const auto& observed = *right;
                    keys_.insert(right_key_(observed));
                }
            }
            do {
                const auto& observed = *item;
                auto        key      = left_key_(observed);
                if constexpr (Mode == 1) {
                    if (keys_.remove(key)) return item;
                } else {
                    if (keys_.insert(rstd::move(key))) return item;
                }
                item = left_.next();
            } while (item.is_some());
        }
        done_ = true;
        return None();
    }
    auto size_hint() const -> SizeHint {
        if (done_) return { usize(), Some(usize()) };
        if constexpr (Mode == 0) return { usize(), None() };
        return { usize(), left_.size_hint().template get<1>() };
    }
};

template<int Mode, typename L, typename R, typename LF, typename RF, typename S, typename Eq>
auto make_set_operation(L&& left, R&& right, LF left_key, RF right_key, S hasher, Eq equal) {
    return SetOperation<into_iter_t<L>, into_iter_t<R>, LF, RF, S, Eq, Mode>(
        iter::into_iter(rstd::forward<L>(left)),
        iter::into_iter(rstd::forward<R>(right)),
        rstd::move(left_key),
        rstd::move(right_key),
        rstd::move(hasher),
        rstd::move(equal));
}

template<typename L,
         typename R,
         typename LF,
         typename RF,
         typename S  = hash::RandomState,
         typename Eq = ::alloc::collections::DefaultHashEqual<query_key_t<into_iter_t<L>, LF>>>
auto union_by(L&& left, R&& right, LF left_key, RF right_key, S hasher = {}, Eq equal = {}) {
    return make_set_operation<0>(rstd::forward<L>(left),
                                 rstd::forward<R>(right),
                                 rstd::move(left_key),
                                 rstd::move(right_key),
                                 rstd::move(hasher),
                                 rstd::move(equal));
}
template<typename L,
         typename R,
         typename LF,
         typename RF,
         typename S  = hash::RandomState,
         typename Eq = ::alloc::collections::DefaultHashEqual<query_key_t<into_iter_t<L>, LF>>>
auto intersection_by(L&& left, R&& right, LF left_key, RF right_key, S hasher = {}, Eq equal = {}) {
    return make_set_operation<1>(rstd::forward<L>(left),
                                 rstd::forward<R>(right),
                                 rstd::move(left_key),
                                 rstd::move(right_key),
                                 rstd::move(hasher),
                                 rstd::move(equal));
}
template<typename L,
         typename R,
         typename LF,
         typename RF,
         typename S  = hash::RandomState,
         typename Eq = ::alloc::collections::DefaultHashEqual<query_key_t<into_iter_t<L>, LF>>>
auto difference_by(L&& left, R&& right, LF left_key, RF right_key, S hasher = {}, Eq equal = {}) {
    return make_set_operation<2>(rstd::forward<L>(left),
                                 rstd::forward<R>(right),
                                 rstd::move(left_key),
                                 rstd::move(right_key),
                                 rstd::move(hasher),
                                 rstd::move(equal));
}

} // namespace rstd::iter
