export module rstd.core:try_;
import :option;
import :result;

namespace rstd::try_
{

export struct NoneFailure {};

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

export template<typename T>
concept TrySource = ResultSource<T> || OptionSource<T>;

export template<TrySource T>
[[nodiscard]]
constexpr auto is_success(const T& source) noexcept -> bool {
    if constexpr (ResultSource<T>) {
        return source.is_ok();
    } else {
        return source.is_some();
    }
}

export template<TrySource T>
constexpr auto take_output(T&& source) {
    using output_type = decltype(rstd::forward<T>(source).unwrap_unchecked());
    return Output<output_type> { rstd::forward<T>(source).unwrap_unchecked() };
}

export template<typename T>
constexpr decltype(auto) finish(Output<T>&& output) {
    return rstd::move(output).take();
}

export template<TrySource T>
constexpr auto take_residual(T&& source) {
    if constexpr (ResultSource<T>) {
        return Err(rstd::forward<T>(source).unwrap_err_unchecked());
    } else {
        return None();
    }
}

export template<TrySource T>
constexpr decltype(auto) take_failure(T&& source) {
    if constexpr (ResultSource<T>) {
        return rstd::forward<T>(source).unwrap_err_unchecked();
    } else {
        return NoneFailure {};
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
