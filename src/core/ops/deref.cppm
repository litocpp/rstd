export module rstd.core:ops.deref;
export import :trait;

namespace rstd
{

export template<typename T>
struct ref;

export template<typename T>
struct mut_ref;

namespace ops
{

/// Provides immutable dereferencing to an associated target type.
export struct Deref {
    template<typename Self, typename = void>
    struct Api {
        using Trait  = Deref;
        using Target = typename Impl<Deref, mtp::rm_cvf<Self>>::Target;

        auto deref() const noexcept -> ref<Target> { return trait_call<0>(this); }
    };

    template<typename T>
        requires requires { typename T::Target; }
    using Funcs = TraitFuncs<&T::deref>;
};

/// The target associated with a `Deref` implementation.
export template<typename Self>
using deref_target_t = typename Impl<Deref, mtp::rm_cvf<Self>>::Target;

/// Provides mutable dereferencing to the target exposed by `Deref`.
export struct DerefMut {
    // Shared handles can explicitly preserve target mutability through a const receiver.
    static constexpr bool allow_const_member_impl { true };

    template<typename Self, typename = void>
        requires Impled<Self, Deref>
    struct Api {
        using Trait  = DerefMut;
        using Target = deref_target_t<Self>;

        auto deref_mut() noexcept -> mut_ref<Target> { return trait_call<0>(this); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::deref_mut>;
};

export template<typename Self>
concept ConstDerefMut =
    Impled<Self, Deref> && Impled<Self, DerefMut> && requires(const Self& self) {
        { self.deref_mut() } -> mtp::same_as<mut_ref<deref_target_t<Self>>>;
    };

export template<typename Self>
    requires Impled<Self, Deref> && (! ConstDerefMut<Self>)
constexpr decltype(auto) deref_value(const Self& self) noexcept {
    auto target = as<Deref>(self).deref();
    if constexpr (requires { target.get(); }) {
        return target.get();
    } else {
        using Pointee = mtp::rm_cv<mtp::rm_ptr<decltype(target.as_raw_ptr())>>;
        if constexpr (mtp::is_void<Pointee>) {
            using Delegate = mtp::rm_ref<decltype(target.operator*())>;
            return Delegate { target.operator*() };
        } else {
            return *target.as_raw_ptr();
        }
    }
}

export template<ConstDerefMut Self>
constexpr decltype(auto) deref_value(const Self& self) noexcept {
    auto target = self.deref_mut();
    if constexpr (requires { target.get_mut(); }) {
        return target.get_mut();
    } else {
        using Pointee = mtp::rm_cv<mtp::rm_ptr<decltype(target.as_raw_ptr())>>;
        if constexpr (mtp::is_void<Pointee>) {
            using Delegate = mtp::rm_ref<decltype(target.operator*())>;
            return Delegate { target.operator*() };
        } else {
            return *target.as_raw_ptr();
        }
    }
}

export template<typename Self>
    requires Impled<Self, DerefMut>
constexpr decltype(auto) deref_value(Self& self) noexcept {
    auto target = as<DerefMut>(self).deref_mut();
    if constexpr (requires { target.get_mut(); }) {
        return target.get_mut();
    } else {
        using Pointee = mtp::rm_cv<mtp::rm_ptr<decltype(target.as_raw_ptr())>>;
        if constexpr (mtp::is_void<Pointee>) {
            using Delegate = mtp::rm_ref<decltype(target.operator*())>;
            return Delegate { target.operator*() };
        } else {
            return *target.as_raw_ptr();
        }
    }
}

export template<typename Self>
    requires Impled<Self, Deref> && (! ConstDerefMut<Self>)
constexpr auto deref_arrow(const Self& self) noexcept {
    auto target = as<Deref>(self).deref();
    if constexpr (requires { target.as_raw_ptr(); }) {
        using Pointee = mtp::rm_cv<mtp::rm_ptr<decltype(target.as_raw_ptr())>>;
        if constexpr (mtp::is_void<Pointee>) {
            return target;
        } else {
            return target.as_raw_ptr();
        }
    } else {
        return target;
    }
}

export template<ConstDerefMut Self>
constexpr auto deref_arrow(const Self& self) noexcept {
    auto target = self.deref_mut();
    if constexpr (requires { target.as_raw_ptr(); }) {
        using Pointee = mtp::rm_cv<mtp::rm_ptr<decltype(target.as_raw_ptr())>>;
        if constexpr (mtp::is_void<Pointee>) {
            return target;
        } else {
            return target.as_raw_ptr();
        }
    } else {
        return target;
    }
}

export template<typename Self>
    requires Impled<Self, DerefMut>
constexpr auto deref_arrow(Self& self) noexcept {
    auto target = as<DerefMut>(self).deref_mut();
    if constexpr (requires { target.as_raw_ptr(); }) {
        using Pointee = mtp::rm_cv<mtp::rm_ptr<decltype(target.as_raw_ptr())>>;
        if constexpr (mtp::is_void<Pointee>) {
            return target;
        } else {
            return target.as_raw_ptr();
        }
    } else {
        return target;
    }
}

} // namespace ops
} // namespace rstd
