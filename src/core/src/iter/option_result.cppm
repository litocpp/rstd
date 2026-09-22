export module rstd.core:iter.option_result;
import :num.types;
export import :iter.sources;
export import :result;

namespace rstd::iter
{

template<has_next I>
struct SuccessState {
    using Wrapped  = typename I::Item;
    using Residual = decltype(try_::take_residual(mtp::declval<Wrapped>()));
    I&               source;
    Option<Residual> failure;
    bool             done {};
};

template<has_next I>
struct SuccessItems : DefaultInClass<SuccessItems<I>, Iterator> {
    using Item                         = checked_item_t<try_::output_t<typename I::Item>>;
    static constexpr bool PROVEN_FUSED = true;
    SuccessState<I>*      state;

    explicit constexpr SuccessItems(SuccessState<I>& value): state(rstd::addressof(value)) {}

    constexpr auto next() -> Option<Item> {
        if (state->done) return None();
        auto value = state->source.next();
        if (value.is_none()) {
            state->done = true;
            return None();
        }
        if (! try_::is_success(*value)) {
            state->failure = Some(try_::take_residual(rstd::forward<typename I::Item>(*value)));
            state->done    = true;
            return None();
        }
        return Some<Item>(try_::finish(try_::take_output(rstd::forward<typename I::Item>(*value))));
    }

    constexpr auto size_hint() const -> SizeHint {
        if (state->done) return { usize(), Some(usize()) };
        return { usize(), as<Iterator>(state->source).size_hint().template get<1>() };
    }
};

template<typename R, has_next I, typename F>
constexpr auto process_successes(I& source, F function) -> R {
    SuccessState<I> state { source, None() };
    auto            output = function(SuccessItems<I>(state));
    if (state.failure.is_some()) return try_::from_residual<R>(rstd::move(*state.failure));
    return try_::from_output<R>(rstd::move(output));
}

export template<typename T>
struct OptionIntoIter : DefaultInClass<OptionIntoIter<T>, Iterator> {
    using Item                                = checked_item_t<T>;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;

    Option<T> value;

    explicit constexpr OptionIntoIter(Option<T> source): value(rstd::move(source)) {}

    constexpr auto next() -> Option<Item> { return value.take(); }
    constexpr auto next_back() -> Option<Item> { return value.take(); }

    constexpr auto size_hint() const -> SizeHint {
        auto length = value.is_some() ? usize(1) : usize();
        return { length, Some(length) };
    }

    constexpr auto len() const -> usize { return value.is_some() ? usize(1) : usize(); }
};

namespace detail
{

template<typename T>
using ImmutableOptionItem = ref<T>;

template<typename T>
using MutableOptionItem = mut_ref<T>;

template<typename T>
constexpr auto borrow_option(ref<Option<T>> source) -> Option<ImmutableOptionItem<T>> {
    auto const* option = source.as_raw_ptr();
    if (option->is_none()) return None();
    return Some(ref<T>::from_raw_parts(rstd::addressof(**option)));
}

template<typename T>
constexpr auto borrow_option_mut(mut_ref<Option<T>> source) -> Option<MutableOptionItem<T>> {
    auto* option = source.as_raw_ptr();
    if (option->is_none()) return None();
    return Some(mut_ref<T>::from_raw_parts(rstd::addressof(**option)));
}

template<typename T, typename E>
constexpr auto borrow_result(ref<Result<T, E>> source) -> Option<ImmutableOptionItem<T>> {
    auto const* result = source.as_raw_ptr();
    if (result->is_err()) return None();
    return Some(ref<T>::from_raw_parts(rstd::addressof(**result)));
}

template<typename T, typename E>
constexpr auto borrow_result_mut(mut_ref<Result<T, E>> source) -> Option<MutableOptionItem<T>> {
    auto* result = source.as_raw_ptr();
    if (result->is_err()) return None();
    return Some(mut_ref<T>::from_raw_parts(rstd::addressof(**result)));
}

} // namespace detail

} // namespace rstd::iter

namespace rstd
{

template<typename T, typename B>
struct Impl<iter::Sum<Option<T>>, Option<B>> : ImplBase<Option<B>> {
    template<iter::has_next I>
    static constexpr auto sum(I source) -> Option<B> {
        return iter::process_successes<Option<B>>(source, [](auto values) {
            return Impl<iter::Sum<T>, B>::sum(rstd::move(values));
        });
    }
};

template<typename T, typename E, typename B>
struct Impl<iter::Sum<Result<T, E>>, Result<B, E>> : ImplBase<Result<B, E>> {
    template<iter::has_next I>
    static constexpr auto sum(I source) -> Result<B, E> {
        return iter::process_successes<Result<B, E>>(source, [](auto values) {
            return Impl<iter::Sum<T>, B>::sum(rstd::move(values));
        });
    }
};

template<typename T, typename B>
struct Impl<iter::Product<Option<T>>, Option<B>> : ImplBase<Option<B>> {
    template<iter::has_next I>
    static constexpr auto product(I source) -> Option<B> {
        return iter::process_successes<Option<B>>(source, [](auto values) {
            return Impl<iter::Product<T>, B>::product(rstd::move(values));
        });
    }
};

template<typename T, typename E, typename B>
struct Impl<iter::Product<Result<T, E>>, Result<B, E>> : ImplBase<Result<B, E>> {
    template<iter::has_next I>
    static constexpr auto product(I source) -> Result<B, E> {
        return iter::process_successes<Result<B, E>>(source, [](auto values) {
            return Impl<iter::Product<T>, B>::product(rstd::move(values));
        });
    }
};

template<typename T, typename B>
struct Impl<iter::FromIterator<Option<T>>, Option<B>> : ImplBase<Option<B>> {
    template<iter::has_next I>
    static constexpr auto from_iter(I source) -> Option<B> {
        return iter::process_successes<Option<B>>(source, [](auto values) {
            return iter::from_iter<B>(rstd::move(values));
        });
    }
};

template<typename T, typename E, typename B>
struct Impl<iter::FromIterator<Result<T, E>>, Result<B, E>> : ImplBase<Result<B, E>> {
    template<iter::has_next I>
    static constexpr auto from_iter(I source) -> Result<B, E> {
        return iter::process_successes<Result<B, E>>(source, [](auto values) {
            return iter::from_iter<B>(rstd::move(values));
        });
    }
};

template<iter::valid_item T>
struct Impl<iter::IntoIterator, Option<T>> : ImplBase<Option<T>> {
    using IntoIter = iter::OptionIntoIter<T>;

    constexpr auto into_iter() -> IntoIter { return IntoIter(rstd::move(this->self())); }
};

template<typename T>
    requires(! mtp::is_ref<T>) &&
            requires(const T& value) { ref<T>::from_raw_parts(rstd::addressof(value)); }
struct Impl<iter::IntoIterator, ref<Option<T>>> : ImplBase<ref<Option<T>>> {
    using IntoIter = iter::OptionIntoIter<iter::detail::ImmutableOptionItem<T>>;

    constexpr auto into_iter() -> IntoIter {
        return IntoIter(iter::detail::borrow_option(this->self()));
    }
};

template<typename T>
    requires(! mtp::is_ref<T>) &&
            requires(T& value) { mut_ref<T>::from_raw_parts(rstd::addressof(value)); }
struct Impl<iter::IntoIterator, mut_ref<Option<T>>> : ImplBase<mut_ref<Option<T>>> {
    using IntoIter = iter::OptionIntoIter<iter::detail::MutableOptionItem<T>>;

    constexpr auto into_iter() -> IntoIter {
        return IntoIter(iter::detail::borrow_option_mut(this->self()));
    }
};

template<iter::valid_item T, typename E>
struct Impl<iter::IntoIterator, Result<T, E>> : ImplBase<Result<T, E>> {
    using IntoIter = iter::OptionIntoIter<T>;

    constexpr auto into_iter() -> IntoIter {
        auto source = rstd::move(this->self());
        return IntoIter(source.ok());
    }
};

template<typename T, typename E>
    requires(! mtp::is_ref<T>) &&
            requires(const T& value) { ref<T>::from_raw_parts(rstd::addressof(value)); }
struct Impl<iter::IntoIterator, ref<Result<T, E>>> : ImplBase<ref<Result<T, E>>> {
    using IntoIter = iter::OptionIntoIter<iter::detail::ImmutableOptionItem<T>>;

    constexpr auto into_iter() -> IntoIter {
        return IntoIter(iter::detail::borrow_result(this->self()));
    }
};

template<typename T, typename E>
    requires(! mtp::is_ref<T>) &&
            requires(T& value) { mut_ref<T>::from_raw_parts(rstd::addressof(value)); }
struct Impl<iter::IntoIterator, mut_ref<Result<T, E>>> : ImplBase<mut_ref<Result<T, E>>> {
    using IntoIter = iter::OptionIntoIter<iter::detail::MutableOptionItem<T>>;

    constexpr auto into_iter() -> IntoIter {
        return IntoIter(iter::detail::borrow_result_mut(this->self()));
    }
};

} // namespace rstd
