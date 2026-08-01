export module rstd.core:error;
export import :error.trait;
export import :any;
export import :fmt;
export import :option;

export namespace rstd::error
{

template<typename Self, typename Delegate>
    requires Impled<Self, fmt::Debug, fmt::Display>
auto Error::Api<Self, Delegate>::source() const noexcept [[clang::lifetimebound]]
-> Option<ErrorRef> {
    return trait_call<0>(this);
}

template<typename T>
    requires Impled<T, Error>
auto is(ErrorRef value) noexcept -> bool {
    return value.concrete_type_id() == any::TypeId::of<T>();
}

template<typename T>
    requires Impled<T, Error>
auto is(mut_ref<dyn<Error>> value) noexcept -> bool {
    return is<T>(value.as_ref());
}

template<typename T>
    requires Impled<T, Error>
auto downcast_ref(ErrorRef value [[clang::lifetimebound]]) noexcept -> Option<ref<T>> {
    if (! is<T>(value)) return None();
    using Storage = typename ref<T>::storage_type;
    return Some(ref<T>::from_raw_parts(static_cast<const Storage*>(value.as_raw_ptr())));
}

template<typename T>
    requires Impled<T, Error>
auto downcast_mut(mut_ref<dyn<Error>> value [[clang::lifetimebound]]) noexcept
    -> Option<mut_ref<T>> {
    if (! is<T>(value)) return None();
    using Storage = typename mut_ref<T>::storage_type;
    return Some(mut_ref<T>::from_raw_parts(static_cast<Storage*>(value.as_raw_ptr())));
}

} // namespace rstd::error

namespace rstd
{

template<typename Self>
    requires mtp::trait_default_tag<Self>
struct Impl<error::Error, Self> : ImplBase<Self> {
    auto source() const noexcept -> Option<error::ErrorRef> { return None(); }
};

template<>
struct Impl<fmt::Display, error::ErrorRef> : ImplBase<error::ErrorRef> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return this->self().template call_super<fmt::Display, 0>(formatter);
    }
};

template<>
struct Impl<fmt::Debug, error::ErrorRef> : ImplBase<error::ErrorRef> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return this->self().template call_super<fmt::Debug, 0>(formatter);
    }
};

template<>
struct Impl<fmt::Display, mut_ref<dyn<error::Error>>> : ImplBase<mut_ref<dyn<error::Error>>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return this->self().template call_super<fmt::Display, 0>(formatter);
    }
};

template<>
struct Impl<fmt::Debug, mut_ref<dyn<error::Error>>> : ImplBase<mut_ref<dyn<error::Error>>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return this->self().template call_super<fmt::Debug, 0>(formatter);
    }
};

} // namespace rstd
