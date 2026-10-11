export module rstd.core:stream.adapters;
export import :stream.traits;

namespace rstd::stream
{

template<Source S, typename F>
struct Map {
    using Item = iter::checked_item_t<mtp::invoke_result_t<F&, stream_item_t<S>>>;
    static_assert(! mtp::is_void<Item>);
    S    source;
    F    function;
    auto poll_next(mut_ref<Map> self, task::Context& cx) -> Poll<Option<Item>> {
        auto out = future::poll_next(self->source, cx);
        if (out.is_pending()) return Poll<Option<Item>>::Pending();
        auto item = rstd::move(out).take();
        if (item.is_none()) return Poll<Option<Item>>::Ready(None());
        return Poll<Option<Item>>::Ready(
            Some<Item>(self->function(rstd::move(item).unwrap_unchecked())));
    }
};

export template<Source S, typename F>
auto map(S source, F function) {
    return Map<S, F> { rstd::move(source), rstd::move(function) };
}

template<Source S, typename F>
struct Inspect {
    using Item = stream_item_t<S>;
    S    source;
    F    function;
    auto poll_next(mut_ref<Inspect> self, task::Context& cx) -> Poll<Option<Item>> {
        auto out = future::poll_next(self->source, cx);
        if (out.is_ready() && out.value().is_some()) {
            self->function(static_cast<const Item&>(*out.value()));
        }
        return out;
    }
};

export template<Source S, typename F>
auto inspect(S source, F function) {
    return Inspect<S, F> { rstd::move(source), rstd::move(function) };
}

template<Source S>
struct Take {
    using Item = stream_item_t<S>;
    S     source;
    usize remaining;
    auto  poll_next(mut_ref<Take> self, task::Context& cx) -> Poll<Option<Item>> {
        if (self->remaining == usize()) return Poll<Option<Item>>::Ready(None());
        auto out = future::poll_next(self->source, cx);
        if (out.is_ready()) {
            if (out.value().is_some())
                --self->remaining;
            else
                self->remaining = usize();
        }
        return out;
    }
};

export template<Source S>
auto take(S source, usize count) {
    return Take<S> { rstd::move(source), count };
}

template<Source S>
struct Fuse {
    using Item = stream_item_t<S>;
    S    source;
    bool done {};
    auto poll_next(mut_ref<Fuse> self, task::Context& cx) -> Poll<Option<Item>> {
        if (self->done) return Poll<Option<Item>>::Ready(None());
        auto out = future::poll_next(self->source, cx);
        if (out.is_ready() && out.value().is_none()) self->done = true;
        return out;
    }
};

export template<Source S>
auto fuse(S source) {
    return Fuse<S> { rstd::move(source) };
}

template<Source A, Source B>
    requires mtp::same_as<stream_item_t<A>, stream_item_t<B>>
struct Chain {
    using Item = stream_item_t<A>;
    Option<A> first;
    B         second;
    auto      poll_next(mut_ref<Chain> self, task::Context& cx) -> Poll<Option<Item>> {
        if (self->first.is_some()) {
            auto out = future::poll_next(*self->first, cx);
            if (out.is_pending() || out.value().is_some()) return out;
            self->first = None();
        }
        return future::poll_next(self->second, cx);
    }
};

export template<Source A, Source B>
    requires mtp::same_as<stream_item_t<A>, stream_item_t<B>>
auto chain(A first, B second) {
    return Chain<A, B> { Some(rstd::move(first)), rstd::move(second) };
}

template<Source S, typename F>
    requires Pollable<mtp::invoke_result_t<F&, stream_item_t<S>>>
class Then {
    using Pending = mtp::invoke_result_t<F&, stream_item_t<S>>;
    S               source_;
    F               function_;
    Option<Pending> pending_;

public:
    using Item = iter::checked_item_t<future::future_output_t<Pending>>;
    static_assert(! mtp::is_void<Item>);
    Then(S source, F function): source_(rstd::move(source)), function_(rstd::move(function)) {}
    auto poll_next(mut_ref<Then> self, task::Context& cx) -> Poll<Option<Item>> {
        if (self->pending_.is_none()) {
            auto out = future::poll_next(self->source_, cx);
            if (out.is_pending()) return Poll<Option<Item>>::Pending();
            auto item = rstd::move(out).take();
            if (item.is_none()) return Poll<Option<Item>>::Ready(None());
            self->pending_.insert(self->function_(rstd::move(item).unwrap_unchecked()));
        }
        auto out = future::poll(*self->pending_, cx);
        if (out.is_pending()) return Poll<Option<Item>>::Pending();
        auto value     = rstd::move(out).take();
        self->pending_ = None();
        return Poll<Option<Item>>::Ready(Some(rstd::move(value)));
    }
};

/// The callback's future must own its inputs or borrow an independently stable owner.
export template<Source S, typename F>
    requires Pollable<mtp::invoke_result_t<F&, stream_item_t<S>>>
auto then(S source, F function) {
    return Then<S, F> { rstd::move(source), rstd::move(function) };
}

} // namespace rstd::stream
