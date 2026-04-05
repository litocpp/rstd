export module rstd.core:convert;
export import :trait;
export import :core;
export import :clone;

namespace rstd::convert
{

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
        static auto from(from_t value) -> Self { trait_call_static<0, From<TF>, Self>(value); }
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
        auto into() -> into_t { return trait_call<0>(this); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::into>;
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
        auto as_ref() const noexcept -> ref<T> { return trait_call<0>(this); }
    };

    template<typename F>
    using Funcs = TraitFuncs<&F::as_ref>;
};

/// Trait for cheaply borrowing data as a mutable reference to T.
///
/// Implementors provide:
/// - `as_mut() noexcept -> ref<T>` : Returns a mutable reference to the inner T.
/// \tparam T The target reference type.
export template<typename T>
struct AsMut {
    template<typename Self, typename = void>
    struct Api {
        auto as_mut() noexcept -> ref<T> { return trait_call<0>(this); }
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
    return Impl<Into<T>, mtp::rm_ref<F>>::into(val);
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
auto as_ref(F& r) noexcept {
    return Impl<AsRef<T>, mtp::rm_ref<F>>::as_ref(r);
}

/// Borrows r as a mutable reference to T via the AsMut trait.
/// \tparam T The target reference type.
/// \tparam F The source type (deduced).
/// \param r The value to borrow from.
export template<typename T, typename F>
auto as_mut(F& r) noexcept {
    return Impl<AsMut<T>, mtp::rm_ref<F>>::as_mut(r);
}
} // namespace rstd::convert

namespace rstd
{

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
    requires mtp::same_as<T, convert::From<mtp::underlying<Self>>> && mtp::is_enum<Self>
struct Impl<T, Self> : ImplBase<Self> {
    using from_t = typename T::from_t;
    static auto from(from_t value) -> Self { return static_cast<Self>(value); }
};

export using convert::as_ref;
export using convert::into;

} // namespace rstd