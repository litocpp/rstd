export module rstd.core:convert.try_;
import :error.trait;
import :fmt;
export import :convert.base;
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
} // namespace rstd::convert

namespace rstd
{

template<>
struct Impl<fmt::Display, convert::Infallible> : ImplBase<convert::Infallible> {
    auto fmt(fmt::Formatter&) const -> bool { rstd::unreachable(); }
};

template<>
struct Impl<fmt::Debug, convert::Infallible> : ImplBase<convert::Infallible> {
    auto fmt(fmt::Formatter&) const -> bool { rstd::unreachable(); }
};

template<>
struct Impl<error::Error, convert::Infallible> : ImplBase<convert::Infallible> {
    auto source() const noexcept -> Option<error::ErrorRef> { rstd::unreachable(); }
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

export using convert::try_from;
export using convert::try_into;

} // namespace rstd
