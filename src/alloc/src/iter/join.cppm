export module rstd.alloc:iter.join;
export import :iter.group;

using namespace rstd::prelude;
using ::alloc::vec::Vec;

template<bool Grouped, typename F, typename O, typename I>
struct JoinOutput;
template<typename F, typename O, typename I>
struct JoinOutput<false, F, O, I> {
    using Type =
        decltype(rstd::mtp::declval<F&>()(rstd::mtp::declval<const rstd::mtp::rm_ref<O>&>(),
                                          rstd::mtp::declval<const I&>()));
};
template<typename F, typename O, typename I>
struct JoinOutput<true, F, O, I> {
    using Type =
        decltype(rstd::mtp::declval<F&>()(rstd::mtp::declval<const rstd::mtp::rm_ref<O>&>(),
                                          rstd::mtp::declval<rstd::slice<I>>()));
};

export namespace rstd::iter
{

template<typename O,
         typename I,
         typename OF,
         typename IF,
         typename F,
         typename S,
         typename Eq,
         bool Grouped>
class Join : public DefaultInClass<Join<O, I, OF, IF, F, S, Eq, Grouped>, Iterator> {
    using InnerItem = stored_item_t<typename I::Item>;
    using Key = decltype(mtp::declval<IF&>()(mtp::declval<const mtp::rm_ref<typename I::Item>&>()));
    using Index = GroupIndex<Key, InnerItem, S, Eq>;
    O                           outer_;
    I                           inner_;
    OF                          outer_key_;
    IF                          inner_key_;
    F                           select_;
    S                           hasher_;
    Eq                          equal_;
    Option<Index>               index_;
    Option<typename O::Item>    current_;
    Option<ref<Vec<InnerItem>>> matches_;
    usize                       position_;
    bool                        done_ = false;

public:
    using Item = typename JoinOutput<Grouped, F, typename O::Item, InnerItem>::Type;
    static_assert(valid_item<Item> && ! borrowed_item<Item> && ! mtp::is_ptr<Item>,
                  "join results must not borrow internal storage");
    static constexpr bool PROVEN_FUSED = true;

    Join(O outer, I inner, OF outer_key, IF inner_key, F select, S hasher, Eq equal)
        : outer_(rstd::move(outer)),
          inner_(rstd::move(inner)),
          outer_key_(rstd::move(outer_key)),
          inner_key_(rstd::move(inner_key)),
          select_(rstd::move(select)),
          hasher_(rstd::move(hasher)),
          equal_(rstd::move(equal)),
          index_(None()),
          current_(None()),
          matches_(None()),
          position_() {}

    auto next() -> Option<Item> {
        if (done_) return None();
        for (;;) {
            if constexpr (! Grouped) {
                if (matches_.is_some() && position_ < (*matches_)->len()) {
                    const auto& outer = *current_;
                    const auto& inner = (**matches_)[position_++];
                    return Some<Item>(select_(outer, inner));
                }
            }
            current_ = outer_.next();
            if (current_.is_none()) {
                done_ = true;
                return None();
            }
            if (index_.is_none()) {
                index_.insert(build_group_index(rstd::move(inner_),
                                                rstd::move(inner_key_),
                                                rstd::move(hasher_),
                                                rstd::move(equal_)));
            }
            const auto& outer = *current_;
            auto        key   = outer_key_(outer);
            matches_          = index_->get(key);
            position_         = usize();
            if constexpr (Grouped) {
                auto group  = matches_.is_some() ? (*matches_)->as_slice() : slice<InnerItem>();
                auto result = Some<Item>(select_(outer, group));
                (void)current_.take();
                return result;
            }
        }
    }

    auto size_hint() const -> SizeHint {
        if (done_) return { usize(), Some(usize()) };
        if constexpr (Grouped) return outer_.size_hint();
        return { usize(), None() };
    }
};

template<typename O,
         typename I,
         typename OF,
         typename IF,
         typename F,
         typename S   = hash::RandomState,
         typename Key = decltype(mtp::declval<IF&>()(
             mtp::declval<const mtp::rm_ref<typename into_iter_t<I>::Item>&>())),
         typename Eq  = ::alloc::collections::DefaultHashEqual<Key>>
auto join_by(O&& outer,
             I&& inner,
             OF  outer_key,
             IF  inner_key,
             F   select,
             S   hasher = {},
             Eq  equal  = {}) {
    return Join<into_iter_t<O>, into_iter_t<I>, OF, IF, F, S, Eq, false>(
        iter::into_iter(rstd::forward<O>(outer)),
        iter::into_iter(rstd::forward<I>(inner)),
        rstd::move(outer_key),
        rstd::move(inner_key),
        rstd::move(select),
        rstd::move(hasher),
        rstd::move(equal));
}

template<typename O,
         typename I,
         typename OF,
         typename IF,
         typename F,
         typename S   = hash::RandomState,
         typename Key = decltype(mtp::declval<IF&>()(
             mtp::declval<const mtp::rm_ref<typename into_iter_t<I>::Item>&>())),
         typename Eq  = ::alloc::collections::DefaultHashEqual<Key>>
auto group_join_by(O&& outer,
                   I&& inner,
                   OF  outer_key,
                   IF  inner_key,
                   F   select,
                   S   hasher = {},
                   Eq  equal  = {}) {
    return Join<into_iter_t<O>, into_iter_t<I>, OF, IF, F, S, Eq, true>(
        iter::into_iter(rstd::forward<O>(outer)),
        iter::into_iter(rstd::forward<I>(inner)),
        rstd::move(outer_key),
        rstd::move(inner_key),
        rstd::move(select),
        rstd::move(hasher),
        rstd::move(equal));
}

} // namespace rstd::iter
