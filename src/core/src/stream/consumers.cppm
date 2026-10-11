export module rstd.core:stream.consumers;
export import :stream.traits;
import :iter.sources;

namespace rstd::stream
{

template<typename T, bool Fallible>
struct ConsumerItem {
    using Value = T;
};
template<typename T>
struct ConsumerItem<T, true> {
    using Value = typename T::value_type;
    using Error = typename T::error_type;
};

template<typename B, typename T, bool Fallible>
struct Collected {
    using Output = B;
};
template<typename B, typename T>
struct Collected<B, T, true> {
    using Output = Result<B, typename T::error_type>;
};

template<typename B, typename T>
concept Collection = requires(B& collection, T value) {
    { rstd::iter::from_iter<B>(rstd::iter::empty<T>()) } -> mtp::same_as<B>;
    rstd::iter::extend(collection, rstd::iter::once(rstd::move(value)));
};

template<typename B, Source S, bool Fallible>
    requires Collection<B, typename ConsumerItem<stream_item_t<S>, Fallible>::Value>
class Collect {
    using Item  = stream_item_t<S>;
    using Value = typename ConsumerItem<Item, Fallible>::Value;
    Option<S> source_;
    Option<B> collection_;

public:
    using Output = typename Collected<B, Item, Fallible>::Output;
    explicit Collect(S source)
        : source_(Some(rstd::move(source))),
          collection_(Some(rstd::iter::from_iter<B>(rstd::iter::empty<Value>()))) {}

    auto poll(mut_ref<Collect> self, task::Context& cx) -> Poll<Output> {
        if (self->source_.is_none()) rstd::panic { "Collect polled after completion" };
        auto budget = PollBudget {};
        while (budget.proceed(cx)) {
            auto out = future::poll_next(*self->source_, cx);
            if (out.is_pending()) return Poll<Output>::Pending();
            auto item = rstd::move(out).take();
            if (item.is_none()) {
                self->source_   = None();
                auto collection = self->collection_.take().unwrap_unchecked();
                if constexpr (Fallible)
                    return Poll<Output>::Ready(Ok(rstd::move(collection)));
                else
                    return Poll<Output>::Ready(rstd::move(collection));
            }
            auto value = rstd::move(item).unwrap_unchecked();
            if constexpr (Fallible) {
                if (value.is_err()) {
                    self->source_     = None();
                    self->collection_ = None();
                    return Poll<Output>::Ready(Err(rstd::move(value).unwrap_err_unchecked()));
                }
                rstd::iter::extend(*self->collection_,
                                   rstd::iter::once(rstd::move(value).unwrap_unchecked()));
            } else {
                rstd::iter::extend(*self->collection_, rstd::iter::once(rstd::move(value)));
            }
        }
        return Poll<Output>::Pending();
    }
};

/// Incremental collection requires FromIterator for an empty seed and Extend for each item.
export template<typename B, Source S>
    requires Collection<B, stream_item_t<S>>
auto collect(S source) {
    return Collect<B, S, false>(rstd::move(source));
}

export template<typename B, Source S>
    requires mtp::spec_of<stream_item_t<S>, Result> &&
             Collection<B, typename stream_item_t<S>::value_type>
auto try_collect(S source) {
    return Collect<B, S, true>(rstd::move(source));
}

template<typename T, bool Fallible>
struct Consumed {
    using Output = rstd::empty;
};
template<typename T>
struct Consumed<T, true> {
    using Output = Result<rstd::empty, typename T::error_type>;
};

template<Source S, typename F, bool Fallible>
class ForEach {
    using Item    = stream_item_t<S>;
    using Value   = typename ConsumerItem<Item, Fallible>::Value;
    using Pending = mtp::invoke_result_t<F&, Value>;
    Option<S>       source_;
    F               function_;
    Option<Pending> pending_;

public:
    using Output = typename Consumed<Item, Fallible>::Output;
    static_assert(Pollable<Pending>);
    static_assert(Fallible ? mtp::same_as<future::future_output_t<Pending>, Output>
                           : (mtp::is_void<future::future_output_t<Pending>> ||
                              mtp::same_as<future::future_output_t<Pending>, rstd::empty>));

    ForEach(S source, F function)
        : source_(Some(rstd::move(source))), function_(rstd::move(function)) {}

    auto poll(mut_ref<ForEach> self, task::Context& cx) -> Poll<Output> {
        if (self->source_.is_none()) rstd::panic { "ForEach polled after completion" };
        auto budget = PollBudget {};
        while (budget.proceed(cx)) {
            if (self->pending_.is_none()) {
                auto out = future::poll_next(*self->source_, cx);
                if (out.is_pending()) return Poll<Output>::Pending();
                auto item = rstd::move(out).take();
                if (item.is_none()) {
                    self->source_ = None();
                    if constexpr (Fallible)
                        return Poll<Output>::Ready(Ok(rstd::empty {}));
                    else
                        return Poll<Output>::Ready(rstd::empty {});
                }
                auto value = rstd::move(item).unwrap_unchecked();
                if constexpr (Fallible) {
                    if (value.is_err()) {
                        self->source_ = None();
                        return Poll<Output>::Ready(Err(rstd::move(value).unwrap_err_unchecked()));
                    }
                    self->pending_.insert(self->function_(rstd::move(value).unwrap_unchecked()));
                } else {
                    self->pending_.insert(self->function_(rstd::move(value)));
                }
            }
            auto out = future::poll(*self->pending_, cx);
            if (out.is_pending()) return Poll<Output>::Pending();
            if constexpr (Fallible) {
                auto result    = rstd::move(out).take();
                self->pending_ = None();
                if (result.is_err()) {
                    self->source_ = None();
                    return Poll<Output>::Ready(rstd::move(result));
                }
            } else {
                (void)rstd::move(out).take();
                self->pending_ = None();
            }
        }
        return Poll<Output>::Pending();
    }
};

export template<Source S, typename F>
    requires Pollable<mtp::invoke_result_t<F&, stream_item_t<S>>>
auto for_each(S source, F function) {
    return ForEach<S, F, false>(rstd::move(source), rstd::move(function));
}

export template<Source S, typename F>
    requires mtp::spec_of<stream_item_t<S>, Result> &&
             Pollable<mtp::invoke_result_t<F&, typename stream_item_t<S>::value_type>>
auto try_for_each(S source, F function) {
    return ForEach<S, F, true>(rstd::move(source), rstd::move(function));
}

} // namespace rstd::stream
