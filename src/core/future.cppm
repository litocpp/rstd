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

template<typename F>
concept FutureInClass = requires(mtp::rm_cvf<F>& future, task::Context& cx) {
    typename mtp::rm_cvf<F>::Output;
    { future.poll(as_mut_ref(future), cx) } -> mtp::same_as<task::Poll<future_output_t<F>>>;
};

template<typename S>
concept StreamInClass = requires(mtp::rm_cvf<S>& stream, task::Context& cx) {
    typename mtp::rm_cvf<S>::Item;
    {
        stream.poll_next(as_mut_ref(stream), cx)
    } -> mtp::same_as<task::Poll<Option<stream_item_t<S>>>>;
};

} // namespace rstd::future

namespace rstd
{

export template<typename F>
    requires future::FutureInClass<F>
struct Impl<future::Future<future::future_output_t<F>>, F>
    : LinkClassMethod<future::Future<future::future_output_t<F>>, F> {};

export template<typename S>
    requires future::StreamInClass<S>
struct Impl<future::Stream<future::stream_item_t<S>>, S>
    : LinkClassMethod<future::Stream<future::stream_item_t<S>>, S> {};

} // namespace rstd

namespace rstd::future
{

export template<typename F>
auto poll(F& future, task::Context& cx) -> task::Poll<future_output_t<F>>
    requires Impled<mtp::rm_cvf<F>, Future<future_output_t<F>>>
{
    return as<Future<future_output_t<F>>>(future).poll(as_mut_ref(future), cx);
}

export template<typename S>
auto poll_next(S& stream, task::Context& cx) -> task::Poll<Option<stream_item_t<S>>>
    requires Impled<mtp::rm_cvf<S>, Stream<stream_item_t<S>>>
{
    return as<Stream<stream_item_t<S>>>(stream).poll_next(as_mut_ref(stream), cx);
}

} // namespace rstd::future
