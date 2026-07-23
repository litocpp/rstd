export module rstd.core:iter.option_result;
export import :iter.sources;
export import :result;

namespace rstd::iter
{

export template<typename T>
struct OptionIntoIter : DefaultInClass<OptionIntoIter<T>, Iterator> {
    using Item                                = T;
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
using ImmutableOptionItem = mtp::cond<mtp::same_as<T, u8>, const T&, ref<T>>;

template<typename T>
using MutableOptionItem = mtp::cond<mtp::same_as<T, u8>, T&, mut_ref<T>>;

template<typename T>
constexpr auto borrow_option(ref<Option<T>> source) -> Option<ImmutableOptionItem<T>> {
    auto const* option = source.as_raw_ptr();
    if (option->is_none()) return None();
    if constexpr (mtp::same_as<T, u8>) {
        return Some<const T&>(**option);
    } else {
        return Some(ref<T>::from_raw_parts(rstd::addressof(**option)));
    }
}

template<typename T>
constexpr auto borrow_option_mut(mut_ref<Option<T>> source) -> Option<MutableOptionItem<T>> {
    auto* option = source.as_raw_ptr();
    if (option->is_none()) return None();
    if constexpr (mtp::same_as<T, u8>) {
        return Some<T&>(**option);
    } else {
        return Some(mut_ref<T>::from_raw_parts(rstd::addressof(**option)));
    }
}

template<typename T, typename E>
constexpr auto borrow_result(ref<Result<T, E>> source) -> Option<ImmutableOptionItem<T>> {
    auto const* result = source.as_raw_ptr();
    if (result->is_err()) return None();
    if constexpr (mtp::same_as<T, u8>) {
        return Some<const T&>(**result);
    } else {
        return Some(ref<T>::from_raw_parts(rstd::addressof(**result)));
    }
}

template<typename T, typename E>
constexpr auto borrow_result_mut(mut_ref<Result<T, E>> source) -> Option<MutableOptionItem<T>> {
    auto* result = source.as_raw_ptr();
    if (result->is_err()) return None();
    if constexpr (mtp::same_as<T, u8>) {
        return Some<T&>(**result);
    } else {
        return Some(mut_ref<T>::from_raw_parts(rstd::addressof(**result)));
    }
}

} // namespace detail

} // namespace rstd::iter

namespace rstd
{

template<typename T>
struct Impl<iter::IntoIterator, Option<T>> : ImplBase<Option<T>> {
    using IntoIter = iter::OptionIntoIter<T>;

    auto into_iter() -> IntoIter { return IntoIter(rstd::move(this->self())); }
};

template<typename T>
    requires(! mtp::is_ref<T>)
struct Impl<iter::IntoIterator, ref<Option<T>>> : ImplBase<ref<Option<T>>> {
    using IntoIter = iter::OptionIntoIter<iter::detail::ImmutableOptionItem<T>>;

    auto into_iter() -> IntoIter { return IntoIter(iter::detail::borrow_option(this->self())); }
};

template<typename T>
    requires(! mtp::is_ref<T>)
struct Impl<iter::IntoIterator, mut_ref<Option<T>>> : ImplBase<mut_ref<Option<T>>> {
    using IntoIter = iter::OptionIntoIter<iter::detail::MutableOptionItem<T>>;

    auto into_iter() -> IntoIter { return IntoIter(iter::detail::borrow_option_mut(this->self())); }
};

template<typename T, typename E>
struct Impl<iter::IntoIterator, Result<T, E>> : ImplBase<Result<T, E>> {
    using IntoIter = iter::OptionIntoIter<T>;

    auto into_iter() -> IntoIter {
        auto source = rstd::move(this->self());
        return IntoIter(source.ok());
    }
};

template<typename T, typename E>
    requires(! mtp::is_ref<T>)
struct Impl<iter::IntoIterator, ref<Result<T, E>>> : ImplBase<ref<Result<T, E>>> {
    using IntoIter = iter::OptionIntoIter<iter::detail::ImmutableOptionItem<T>>;

    auto into_iter() -> IntoIter { return IntoIter(iter::detail::borrow_result(this->self())); }
};

template<typename T, typename E>
    requires(! mtp::is_ref<T>)
struct Impl<iter::IntoIterator, mut_ref<Result<T, E>>> : ImplBase<mut_ref<Result<T, E>>> {
    using IntoIter = iter::OptionIntoIter<iter::detail::MutableOptionItem<T>>;

    auto into_iter() -> IntoIter { return IntoIter(iter::detail::borrow_result_mut(this->self())); }
};

} // namespace rstd
