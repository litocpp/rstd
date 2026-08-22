export module rstd.core:try_;
import :convert;
import :option;
import :result;
import :ops.control_flow;

namespace rstd::try_
{

export struct NoneFailure {};

/// Owns a failed Result's error until it is returned as an exact or Into-backed Result error.
export template<typename E>
class ResultResidual {
    E error_;

public:
    explicit constexpr ResultResidual(E&& error): error_(rstd::move(error)) {}

    template<typename T, typename F>
        requires mtp::same_as<E, F> || Impled<E, convert::Into<F>>
    constexpr operator result::Result<T, F>() && {
        if constexpr (mtp::same_as<E, F>) {
            return Err(rstd::move(error_));
        } else {
            return Err(rstd::into<F>(rstd::move(error_)));
        }
    }
};

export template<typename E>
class ResultResidual<E&> {
    E* error_;

public:
    explicit constexpr ResultResidual(E& error): error_(rstd::addressof(error)) {}

    template<typename T, typename F>
        requires mtp::same_as<E&, F>
    constexpr operator result::Result<T, F>() && {
        return Err<E&>(*error_);
    }
};

export template<typename B>
class ControlFlowResidual {
    using Stored = mtp::cond<mtp::is_ref<B>, mtp::add_ptr<mtp::rm_ref<B>>, B>;

    Stored value_;

    static constexpr decltype(auto) store(B&& value) {
        if constexpr (mtp::is_ref<B>)
            return rstd::addressof(value);
        else
            return rstd::move(value);
    }

public:
    explicit constexpr ControlFlowResidual(B&& value): value_(store(rstd::forward<B>(value))) {}

    template<typename C>
    constexpr operator ops::ControlFlow<B, C>() && {
        if constexpr (mtp::is_ref<B>)
            return ops::ControlFlow<B, C>::Break(static_cast<B>(*value_));
        else
            return ops::ControlFlow<B, C>::Break(rstd::move(value_));
    }
};

export template<typename T>
class Output {
    T value_;

public:
    template<typename U>
    explicit constexpr Output(U&& value): value_(rstd::forward<U>(value)) {}

    constexpr auto take() && -> T { return rstd::move(value_); }
};

template<typename T>
class Output<T&> {
    T* value_;

public:
    explicit constexpr Output(T& value): value_(rstd::addressof(value)) {}

    constexpr auto take() && -> T& { return *value_; }
};

template<typename T>
class Output<T&&> {
    T* value_;

public:
    explicit constexpr Output(T&& value): value_(rstd::addressof(value)) {}

    constexpr auto take() && -> T&& { return rstd::move(*value_); }
};

template<typename T>
concept ResultSource = mtp::spec_of<mtp::rm_cvf<T>, result::Result>;

template<typename T>
concept OptionSource = mtp::spec_of<mtp::rm_cvf<T>, option::Option>;

template<typename T>
concept ControlFlowSource = mtp::spec_of<mtp::rm_cvf<T>, ops::ControlFlow>;

export template<typename T>
concept TrySource = ResultSource<T> || OptionSource<T> || ControlFlowSource<T>;

template<typename T>
struct SourceTraits;

template<typename T>
struct SourceTraits<option::Option<T>> {
    using Output = T;
};

template<typename T, typename E>
struct SourceTraits<result::Result<T, E>> {
    using Output = T;
};

template<typename B, typename C>
struct SourceTraits<ops::ControlFlow<B, C>> {
    using Output = C;
};

export template<TrySource T>
using output_t = typename SourceTraits<mtp::rm_cvf<T>>::Output;

export template<TrySource T>
[[nodiscard]]
constexpr auto is_success(const T& source) noexcept -> bool {
    if constexpr (ResultSource<T>) {
        return source.is_ok();
    } else if constexpr (OptionSource<T>) {
        return source.is_some();
    } else {
        return source.is_continue();
    }
}

export template<TrySource T>
constexpr auto take_output(T&& source) {
    if constexpr (ControlFlowSource<T>) {
        using output_type = decltype(rstd::forward<T>(source).continue_value_unchecked());
        return Output<output_type> { rstd::forward<T>(source).continue_value_unchecked() };
    } else {
        using output_type = decltype(rstd::forward<T>(source).unwrap_unchecked());
        return Output<output_type> { rstd::forward<T>(source).unwrap_unchecked() };
    }
}

export template<typename T>
constexpr decltype(auto) finish(Output<T>&& output) {
    return rstd::move(output).take();
}

export template<TrySource T>
constexpr auto take_residual(T&& source) {
    if constexpr (ResultSource<T>) {
        using Error = typename mtp::rm_cvf<T>::error_type;
        return ResultResidual<Error> { rstd::forward<T>(source).unwrap_err_unchecked() };
    } else if constexpr (OptionSource<T>) {
        return None();
    } else {
        using Break = typename mtp::rm_cvf<T>::break_type;
        return ControlFlowResidual<Break> { rstd::forward<T>(source).break_value_unchecked() };
    }
}

export template<TrySource R, typename T>
constexpr auto from_output(T&& value) -> mtp::rm_cvf<R> {
    using Source = mtp::rm_cvf<R>;
    if constexpr (ResultSource<Source>) {
        return Source(Ok(rstd::forward<T>(value)));
    } else if constexpr (OptionSource<Source>) {
        return Source(Some<output_t<Source>>(rstd::forward<T>(value)));
    } else {
        return Source::Continue(rstd::forward<T>(value));
    }
}

export template<TrySource R, typename Residual>
constexpr auto from_residual(Residual&& residual) -> mtp::rm_cvf<R> {
    using Source = mtp::rm_cvf<R>;
    if constexpr (OptionSource<Source>)
        return None<output_t<Source>>();
    else
        return Source(rstd::forward<Residual>(residual));
}

export template<TrySource T>
constexpr decltype(auto) take_failure(T&& source) {
    if constexpr (ResultSource<T>) {
        return rstd::forward<T>(source).unwrap_err_unchecked();
    } else if constexpr (OptionSource<T>) {
        return NoneFailure {};
    } else {
        return rstd::forward<T>(source).break_value_unchecked();
    }
}

export template<typename F, typename E>
constexpr decltype(auto) resolve_fallback(F&& fallback, E&& error) {
    if constexpr (mtp::is_invocable_v<F, E>) {
        return rstd::forward<F>(fallback)(rstd::forward<E>(error));
    } else if constexpr (mtp::is_invocable_v<F>) {
        return rstd::forward<F>(fallback)();
    } else {
        return rstd::forward<F>(fallback);
    }
}

export template<typename T>
constexpr decltype(auto) into_residual(T&& value) {
    using value_type = mtp::rm_cvf<T>;
    if constexpr (ResultSource<value_type> || OptionSource<value_type> ||
                  mtp::same_as<value_type, option::Unknown>) {
        return rstd::forward<T>(value);
    } else {
        return Err(rstd::forward<T>(value));
    }
}

} // namespace rstd::try_
