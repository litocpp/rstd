export module rstd.core:future;
export import :task;
export import :option;
export import :trait;

namespace rstd::future
{

export template<typename T>
constexpr auto as_mut_ref(T& value) noexcept -> mut_ref<T> {
    return mut_ref<T>::from_raw_parts(rstd::addressof(value));
}

export template<typename F>
using future_output_t = typename mtp::rm_cvf<F>::Output;

export template<typename S>
using stream_item_t = typename mtp::rm_cvf<S>::Item;

export template<typename Output>
struct Future {
    template<class Self, class Delegate = void>
    struct Api {
        using Trait = Future;

        auto poll(mut_ref<Self> self, task::Context& cx) -> task::Poll<Output> {
            return trait_call<0>(this, self, cx);
        }
    };

    template<typename F>
    using Funcs = TraitFuncs<&F::poll>;
};

export template<typename Item>
struct Stream {
    template<class Self, class Delegate = void>
    struct Api {
        using Trait = Stream;

        auto poll_next(mut_ref<Self> self, task::Context& cx) -> task::Poll<Option<Item>> {
            return trait_call<0>(this, self, cx);
        }
    };

    template<typename F>
    using Funcs = TraitFuncs<&F::poll_next>;
};

} // namespace rstd::future

namespace rstd::future
{

export template<typename F>
auto poll(F& future, task::Context& cx) -> task::Poll<future_output_t<F>>
    requires Impled<mtp::rm_cvf<F>, Future<future_output_t<F>>>
{
    return as<Future<future_output_t<F>>>(future).poll(as_mut_ref(future), cx);
}

/// Non-owning mutable borrow. Keep the source alive and unmoved; do not poll it through other aliases.
export template<typename F>
    requires Impled<F, Future<future_output_t<F>>>
class FutureRef {
    F* source_;

public:
    using Output = future_output_t<F>;

    explicit FutureRef(F& source [[clang::lifetimebound]]) noexcept
        : source_(rstd::addressof(source)) {}
    FutureRef(const FutureRef&)                    = delete;
    auto operator=(const FutureRef&) -> FutureRef& = delete;
    FutureRef(FutureRef&& other) noexcept: source_(rstd::exchange(other.source_, nullptr)) {}
    auto operator=(FutureRef&& other) noexcept -> FutureRef& {
        if (this != &other) source_ = rstd::exchange(other.source_, nullptr);
        return *this;
    }

    auto poll(mut_ref<FutureRef> self, task::Context& cx) -> task::Poll<Output> {
        if (! self->source_) rstd::panic { "FutureRef polled after move" };
        return future::poll(*self->source_, cx);
    }
};

export template<typename F>
    requires(! mtp::is_const<F>) && Impled<F, Future<future_output_t<F>>>
auto by_ref(F& source [[clang::lifetimebound]]) noexcept -> FutureRef<F> {
    return FutureRef<F>(source);
}

export template<typename S>
auto poll_next(S& stream, task::Context& cx) -> task::Poll<Option<stream_item_t<S>>>
    requires Impled<mtp::rm_cvf<S>, Stream<stream_item_t<S>>>
{
    return as<Stream<stream_item_t<S>>>(stream).poll_next(as_mut_ref(stream), cx);
}

} // namespace rstd::future
