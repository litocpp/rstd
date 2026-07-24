export module rstd.core:convert;
import :error.trait;
import :fmt;
export import :trait;
export import :core;
export import :clone;
export import :result;

namespace rstd::convert
{

/// Error type for conversions that cannot fail.
export class Infallible final {
public:
    Infallible()                  = delete;
    Infallible(const Infallible&) = default;
    Infallible(Infallible&&)      = default;
};

/// Trait for constructing a type from another type, analogous to Rust's `From`.
///
/// Implementors provide:
/// - `static from(from_t value) -> Self` : Creates Self from a value of type TF.
/// \tparam TF The source type to convert from.
export template<typename TF>
struct From {
    using from_t = TF;
    template<typename Self, typename = void>
    struct Api {
        using Trait = From;
        static auto from(from_t value) -> Self {
            return trait_static_call<0, Api>(rstd::move(value));
        }
    };
    template<typename T>
    using Funcs = TraitFuncs<&T::from>;
};

/// Trait for consuming self and producing a value of another type, analogous to Rust's `Into`.
///
/// Implementors provide:
/// - `into() -> into_t` : Converts self into the target type TF.
/// \tparam TF The target type to convert into.
export template<typename TF>
struct Into {
    using into_t = TF;
    template<typename Self, typename = void>
    struct Api {
        using Trait = Into;
        auto into() -> into_t { return trait_call<0>(this); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::into>;
};

/// Trait for checked conversions from another type.
/// \tparam TF The source type to convert from.
export template<typename TF>
struct TryFrom {
    using from_t = TF;
    template<typename Self, typename = void>
    struct Api {
        using Trait = TryFrom;
        using Error = typename Impl<TryFrom, Self>::Error;
        static auto try_from(from_t value) -> Result<Self, Error> {
            return trait_static_call<0, Api>(rstd::move(value));
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::try_from>;
};

/// Trait for consuming self in a checked conversion to another type.
/// \tparam TF The target type to convert into.
export template<typename TF>
struct TryInto {
    using into_t = TF;
    template<typename Self, typename = void>
    struct Api {
        using Trait = TryInto;
        using Error = typename Impl<TryInto, Self>::Error;
        auto try_into() -> Result<into_t, Error> { return trait_call<0>(this); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::try_into>;
};

/// Trait for cheaply borrowing data as an immutable reference to T.
///
/// Implementors provide:
/// - `as_ref() const noexcept -> ref<T>` : Returns a read-only reference to the inner T.
/// \tparam T The target reference type.
export template<typename T>
struct AsRef {
    template<typename Self, typename = void>
    struct Api {
        using Trait = AsRef;
        auto as_ref() const noexcept -> ref<T> { return trait_call<0>(this); }
    };

    template<typename F>
    using Funcs = TraitFuncs<&F::as_ref>;
};

/// Trait for cheaply borrowing data as a mutable reference to T.
///
/// Implementors provide:
/// - `as_mut() noexcept -> mut_ref<T>` : Returns a mutable reference to the inner T.
/// \tparam T The target reference type.
export template<typename T>
struct AsMut {
    template<typename Self, typename = void>
    struct Api {
        using Trait = AsMut;
        auto as_mut() noexcept -> mut_ref<T> { return trait_call<0>(this); }
    };

    template<typename F>
    using Funcs = TraitFuncs<&F::as_mut>;
};

/// Converts a value of type F into type T using the Into trait.
/// \tparam T The target type.
/// \tparam F The source type (deduced).
/// \param val The value to convert.
/// \return The converted value of type T.
export template<typename T, typename F>
auto into(F&& val) -> T {
    return as<Into<T>>(val).into();
}

/// Attempts to construct T from value through TryFrom.
export template<typename T, typename F>
auto try_from(F&& value) {
    using Target = mtp::rm_cvf<T>;
    using Source = mtp::rm_cvf<F>;
    using Trait  = TryFrom<Source>;
    return Trait::template Api<Target>::try_from(rstd::forward<F>(value));
}

/// Attempts to convert value to T through TryInto.
export template<typename T, typename F>
auto try_into(F&& value) {
    using Target = mtp::rm_cvf<T>;
    if constexpr (mtp::is_const<mtp::rm_ref<F>>) {
        auto copy = value;
        return as<TryInto<Target>>(copy).try_into();
    } else {
        return as<TryInto<Target>>(value).try_into();
    }
}

template<typename T>
struct IntoWrapper {
    T&& self;
    template<typename U>
        requires Impled<T, convert::Into<mtp::rm_cv<U>>>
    operator U() {
        if constexpr (Impled<mtp::rm_cv<U>, convert::From<T>>) {
            return Impl<convert::From<T>, mtp::rm_cv<U>>::from(rstd::move(self));
        } else {
            using Trait = convert::Into<mtp::rm_cv<U>>;
            if constexpr (mtp::is_const<T>) {
                if constexpr (Impled<clone::Clone, mtp::rm_cv<U>>) {
                    auto tmp = as<clone::Clone>(self).clone();
                    return as<Trait>(tmp).into();
                } else {
                    auto tmp = self;
                    return as<Trait>(tmp).into();
                }
            } else {
                return as<Trait>(self).into();
            }
        }
    }

    IntoWrapper(T&& t): self(rstd::move(t)) {}
    IntoWrapper(const IntoWrapper&)            = delete;
    IntoWrapper& operator=(const IntoWrapper&) = delete;
    IntoWrapper(IntoWrapper&&)                 = default;
    IntoWrapper& operator=(IntoWrapper&&)      = default;
};

/// Returns an IntoWrapper that defers conversion, enabling implicit conversion via operator U().
/// \tparam T The source type (deduced).
/// \param t The value to wrap for deferred conversion.
/// \return An IntoWrapper holding the value.
export template<typename T>
auto into(T&& t) -> IntoWrapper<mtp::rm_ref<T>> {
    return { rstd::move(t) };
}

/// Borrows r as an immutable reference to T via the AsRef trait.
/// \tparam T The target reference type.
/// \tparam F The source type (deduced).
/// \param r The value to borrow from.
export template<typename T, typename F>
auto as_ref(F& r [[clang::lifetimebound]]) noexcept {
    return as<AsRef<T>>(r).as_ref();
}

/// Borrows r as a mutable reference to T via the AsMut trait.
/// \tparam T The target reference type.
/// \tparam F The source type (deduced).
/// \param r The value to borrow from.
export template<typename T, typename F>
auto as_mut(F& r [[clang::lifetimebound]]) noexcept {
    return as<AsMut<T>>(r).as_mut();
}
} // namespace rstd::convert

namespace rstd
{

template<>
struct Impl<fmt::Display, convert::Infallible> : ImplBase<convert::Infallible> {
    auto fmt(fmt::Formatter&) const -> bool { __builtin_unreachable(); }
};

template<>
struct Impl<fmt::Debug, convert::Infallible> : ImplBase<convert::Infallible> {
    auto fmt(fmt::Formatter&) const -> bool { __builtin_unreachable(); }
};

template<>
struct Impl<error::Error, convert::Infallible> : ImplBase<convert::Infallible> {
    auto source() const noexcept -> Option<error::ErrorRef> { __builtin_unreachable(); }
};

template<typename T, typename Self>
    requires mtp::same_as<T, convert::Into<typename T::into_t>> &&
             Impled<typename T::into_t, typename convert::From<Self>>
struct Impl<T, Self> : ImplBase<Self> {
    using into_t = typename T::into_t;
    auto into() -> into_t {
        return Impl<convert::From<Self>, into_t>::from(rstd::move(this->self()));
    }
};

template<typename T, typename Self>
    requires mtp::same_as<T, convert::TryInto<typename T::into_t>> &&
             Impled<typename T::into_t, typename convert::TryFrom<Self>>
struct Impl<T, Self> : ImplBase<Self> {
    using into_t = typename T::into_t;
    using Error  = typename Impl<convert::TryFrom<Self>, into_t>::Error;

    auto try_into() -> Result<into_t, Error> {
        return convert::try_from<into_t>(rstd::move(this->self()));
    }
};

template<typename T, typename Self>
    requires mtp::same_as<T, convert::TryFrom<typename T::from_t>> &&
             (mtp::same_as<typename T::from_t, Self> ||
              Impled<typename T::from_t, convert::Into<Self>>)
struct Impl<T, Self> {
    using from_t = typename T::from_t;
    using Error  = convert::Infallible;

    static auto try_from(from_t value) -> Result<Self, Error> {
        if constexpr (mtp::same_as<from_t, Self>) {
            return Ok(rstd::move(value));
        } else {
            return Ok(convert::into<Self>(rstd::move(value)));
        }
    }
};

export using convert::as_ref;
export using convert::as_mut;
export using convert::into;
export using convert::try_from;
export using convert::try_into;

} // namespace rstd
