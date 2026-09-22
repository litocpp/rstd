export module rstd.alloc:iter.unique;
export import :iter.storage;
export import :collections.hash_set;

using namespace rstd::prelude;

export namespace rstd::iter
{

template<typename I, typename F, typename S, typename Eq>
class UniqueBy : public DefaultInClass<UniqueBy<I, F, S, Eq>, Iterator> {
    using Key = decltype(mtp::declval<F&>()(mtp::declval<const mtp::rm_ref<typename I::Item>&>()));
    I                                         source_;
    F                                         key_;
    ::alloc::collections::HashSet<Key, S, Eq> seen_;
    bool                                      done_ = false;

public:
    using Item                         = typename I::Item;
    static constexpr bool PROVEN_FUSED = true;
    UniqueBy(I source, F key, S hasher, Eq equal)
        : source_(rstd::move(source)),
          key_(rstd::move(key)),
          seen_(
              ::alloc::collections::HashSet<Key, S, Eq>::with_hasher_and_equal(rstd::move(hasher),
                                                                               rstd::move(equal))) {
        check_persistent_key<Item, Key>();
    }
    auto next() -> Option<Item> {
        if (done_) return None();
        for (auto item = source_.next(); item.is_some(); item = source_.next()) {
            const auto& observed = *item;
            if (seen_.insert(key_(observed))) return item;
        }
        done_ = true;
        return None();
    }
    auto size_hint() const -> SizeHint {
        return { usize(), done_ ? Some(usize()) : source_.size_hint().template get<1>() };
    }
};

template<typename Input,
         typename F,
         typename S   = hash::RandomState,
         typename Key = decltype(mtp::declval<F&>()(
             mtp::declval<const mtp::rm_ref<typename into_iter_t<Input>::Item>&>())),
         typename Eq  = ::alloc::collections::DefaultHashEqual<Key>>
    requires(! mtp::is_ref<Input>) && into_iterable<Input>
auto unique_by(Input&& source, F key, S hasher = {}, Eq equal = {}) {
    return UniqueBy<into_iter_t<Input>, F, S, Eq>(iter::into_iter(rstd::forward<Input>(source)),
                                                  rstd::move(key),
                                                  rstd::move(hasher),
                                                  rstd::move(equal));
}

template<typename Input>
    requires(! mtp::is_ref<Input>) && into_iterable<Input>
auto unique(Input&& source) {
    return unique_by(rstd::forward<Input>(source), [](const auto& item) {
        return as<clone::Clone>(item).clone();
    });
}

} // namespace rstd::iter
