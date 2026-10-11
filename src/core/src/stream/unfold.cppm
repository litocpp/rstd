export module rstd.core:stream.unfold;
export import :stream.traits;

namespace rstd::stream
{

template<typename State, typename F>
    requires Pollable<mtp::invoke_result_t<F&, State>>
class Unfold {
    using Pending = mtp::invoke_result_t<F&, State>;
    using Step    = future::future_output_t<Pending>;
    using Pair    = typename Step::value_type;
    Option<State>   state_;
    Option<Pending> pending_;
    F               function_;
    bool            done_ {};

public:
    using Item =
        rstd::iter::checked_item_t<mtp::rm_cvf<decltype(mtp::declval<Pair>().template get<0>())>>;
    static_assert(mtp::same_as<Step, Option<tuple<Item, State>>>);
    Unfold(State state, F function)
        : state_(Some(rstd::move(state))), function_(rstd::move(function)) {}
    auto poll_next(mut_ref<Unfold> self, task::Context& cx) -> Poll<Option<Item>> {
        if (self->done_) return Poll<Option<Item>>::Ready(None());
        if (self->pending_.is_none()) {
            self->pending_.insert(self->function_(self->state_.take().unwrap_unchecked()));
        }
        auto out = future::poll(*self->pending_, cx);
        if (out.is_pending()) return Poll<Option<Item>>::Pending();
        auto step      = rstd::move(out).take();
        self->pending_ = None();
        if (step.is_none()) {
            self->done_ = true;
            return Poll<Option<Item>>::Ready(None());
        }
        auto pair = rstd::move(step).unwrap_unchecked();
        self->state_.insert(rstd::move(pair.template get<1>()));
        return Poll<Option<Item>>::Ready(Some(rstd::move(pair.template get<0>())));
    }
};

/// The callback owns the state and returns the item together with the next state.
export template<typename State, typename F>
    requires(! mtp::is_ref<State>) && Pollable<mtp::invoke_result_t<F&, State>>
auto unfold(State state, F function) {
    return Unfold<State, F>(rstd::move(state), rstd::move(function));
}

} // namespace rstd::stream
