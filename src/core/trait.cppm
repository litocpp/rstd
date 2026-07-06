export module rstd.core:trait;
export import rstd.basic;

namespace rstd
{

/// A compile-time collection of function pointers representing a trait's API.
/// \tparam Api The member function pointers that form the trait interface.
export template<auto... Api>
struct TraitFuncs {};

/// The trait implementation specialization point.
/// Users specialize this struct to implement trait T for type A.
/// \tparam T The trait type to implement.
/// \tparam A The concrete type implementing the trait.
export template<typename T, typename A>
struct Impl;
namespace mtp
{
template<typename T>
struct TraitApiTraits;
}

namespace mtp
{
/// Checks whether T is a valid trait API type with an associated Trait and type info.
export template<typename T>
concept is_trait_api = requires() {
    typename T::Trait;
    typename mtp::TraitApiTraits<T>::type;
};

/// Checks whether T is a direct trait (implemented directly on the type, not via Impl).
export template<typename T>
concept is_direct_trait = T::direct;

/// Checks whether T has an Api member template defining trait methods.
export template<typename T>
concept has_trait_api = requires { T::Api; };

} // namespace mtp

namespace ptr_
{
/// Type-erased delegate for dynamic dispatch of trait T through a vtable.
/// \tparam T The trait type to dispatch dynamically.
export template<typename T>
struct dyn_delegate;
} // namespace ptr_

/// Tag type used to select a default trait implementation for type T.
/// \tparam T The type for which to provide a default implementation.
export template<typename T>
struct default_tag {};

/// Tag type used to select default trait methods inherited directly by T.
/// \tparam T The type inheriting the default implementation.
export template<typename T>
struct in_class_default_tag {};

struct in_class_tag {};

struct dyn_tag {};

namespace mtp
{

struct ImplHelper;

template<typename T>
struct TraitFuncsHelper;

template<auto... Api>
struct TraitFuncsHelper<TraitFuncs<Api...>> {
    template<template<class...> typename T>
    consteval static auto make() {
        return T { Api... };
    }

    template<usize I>
    consteval static auto get() {
        return mtp::get_auto<I, Api...>();
    }

    consteval static auto size() { return sizeof...(Api); }

    template<usize I>
    using type_at = decltype(get<I>());
};

template<typename T>
struct ImplWithPtr {
    usizeptr ptr_;

    friend struct ImplHelper;

    template<typename P>
    constexpr ImplWithPtr(P* p) noexcept: ptr_(rstd::bit_cast<usizeptr>(p)) {}

protected:
    constexpr auto self() noexcept -> T& { return *rstd::bit_cast<T*>(ptr_); }
    constexpr auto self() const noexcept -> T const& { return *rstd::bit_cast<T const*>(ptr_); }
};

template<template<typename, typename> class T, typename A, typename B>
struct TraitApiTraits<T<A, B>> {
    using type          = A;
    using delegate_type = B;
};

template<typename Trait, typename A, typename Delegate = void>
using TraitApi = typename Trait::template Api<A, Delegate>;

template<typename Trait, typename A>
using TraitFuncs = typename Trait::template Funcs<A>;

template<typename Trait, typename A>
using TraitApiHelper = TraitFuncsHelper<TraitFuncs<Trait, A>>;

template<typename Trait, typename A>
using RequiredTraitFuncs = typename Trait::template RequiredFuncs<A>;

template<typename Trait, typename A>
using RequiredTraitApiHelper = TraitFuncsHelper<RequiredTraitFuncs<Trait, A>>;

template<typename Trait, typename A, typename B, usize Index = 0>
consteval bool check_apis() {
    using ApiHelperA = TraitApiHelper<Trait, A>;
    using ApiHelperB = TraitApiHelper<Trait, B>;

    if constexpr (Index == 0) {
        static_assert(ApiHelperA::size() == ApiHelperB::size(), "Please implement all Trait api");
    }

    if constexpr (Index < ApiHelperA::size()) {
        using T1 = ApiHelperA::template type_at<Index>;
        using T2 = ApiHelperB::template type_at<Index>;

        using F1 = mtp::func_traits<T1>;
        using F2 = mtp::func_traits<T2>;
        using P1 = typename F1::primary;
        using P2 = typename F2::primary;

        static_assert(F1::is_member == F2::is_member);

        if constexpr (F1::is_member) {
            // Is member func
            static_assert(mtp::is_ref_lv<P1> == mtp::is_ref_lv<P2> &&
                              mtp::is_ref_rv<P1> == mtp::is_ref_rv<P2> &&
                              mtp::is_const<mtp::rm_ref<P1>> ==
                                  mtp::is_const<mtp::rm_ref<P2>> &&
                              mtp::same_as<typename F1::to_dyn, typename F2::to_dyn>,
                          "Trait api not satisfy");
        } else {
            // not member func, full compare
            static_assert(mtp::same_as<T1, T2>, "Trait api not satisfy");
        }
        return check_apis<Trait, A, B, Index + 1>();
    }
    return true;
}

template<typename... Api, template<class...> typename Tuple>
consteval auto to_dyn(Tuple<Api...>) {
    return Tuple { (typename mtp::func_traits<Api>::to_dyn)(nullptr)... };
}
struct DynHelper {
    template<typename TDyn>
    static auto get_self(TDyn* t) noexcept {
        return t->p;
    }

    template<typename TDyn>
    static const auto* get_apis(TDyn const* t) noexcept {
        return rstd::addressof(t->vtable->apis);
    }
};

struct ImplHelper {
    template<typename T>
    static auto& get_self(T* t) noexcept {
        return t->self();
    }
};

/// Validates that ToCheck correctly implements all API methods for trait T with type A.
/// \tparam T The trait type.
/// \tparam A The type parameter for the trait API.
/// \tparam ToCheck The type whose API methods are validated.
/// \return true if the APIs match.
export template<typename T, typename A, typename ToCheck>
consteval bool check_trait_apis() {
    static_assert(mtp::has_trait_api<T>);
    return check_apis<T, TraitApi<T, A>, ToCheck>();
}

template<typename...>
constexpr bool dependent_false = false;

enum class trait_impl_kind
{
    None,
    Dyn,
    Direct,
    External,
    InClass
};

enum class trait_impl_failure_reason
{
    None,
    NoImpl,
    ExternalUnavailable,
    ExternalApiMismatch,
    InClassApiMismatch,
    DirectApiMismatch
};

template<typename Trait, typename A>
using external_trait_impl_t = Impl<Trait, A>;

template<typename Trait, typename A>
using in_class_trait_impl_t = Impl<Trait, in_class_default_tag<A>>;

template<typename T>
struct trait_default_tag_traits;

template<typename A>
struct trait_default_tag_traits<default_tag<A>> {
    using self = A;
};

template<typename A>
struct trait_default_tag_traits<in_class_default_tag<A>> {
    using self = A;
};

template<typename T>
concept trait_default_tag = requires { typename trait_default_tag_traits<T>::self; };

template<typename T>
using trait_default_self_t = typename trait_default_tag_traits<T>::self;

template<typename Trait, typename A>
concept has_external_trait_impl = mtp::complete<external_trait_impl_t<Trait, A>>;

template<typename Trait, typename A>
concept has_in_class_trait_impl =
    mtp::complete<A> && requires(A& a) { static_cast<in_class_trait_impl_t<Trait, A>&>(a); };

template<typename Trait, typename A, typename ToCheck>
concept trait_apis_formable = mtp::has_trait_api<Trait> && requires {
    typename TraitApi<Trait, A>;
    typename TraitApiHelper<Trait, TraitApi<Trait, A>>;
    typename TraitApiHelper<Trait, ToCheck>;
};

template<typename Trait, typename ExpectedApi, typename ToCheck, usize Index = 0>
consteval bool check_apis_quiet() {
    using ApiHelperA = TraitApiHelper<Trait, ExpectedApi>;
    using ApiHelperB = TraitApiHelper<Trait, ToCheck>;

    if constexpr (Index == 0) {
        if constexpr (ApiHelperA::size() != ApiHelperB::size()) {
            return false;
        }
    }

    if constexpr (Index < ApiHelperA::size()) {
        using T1 = ApiHelperA::template type_at<Index>;
        using T2 = ApiHelperB::template type_at<Index>;

        using F1 = mtp::func_traits<T1>;
        using F2 = mtp::func_traits<T2>;
        using P1 = typename F1::primary;
        using P2 = typename F2::primary;

        if constexpr (F1::is_member != F2::is_member) {
            return false;
        } else if constexpr (F1::is_member) {
            if constexpr (! (mtp::is_ref_lv<P1> == mtp::is_ref_lv<P2> &&
                             mtp::is_ref_rv<P1> == mtp::is_ref_rv<P2> &&
                             mtp::is_const<mtp::rm_ref<P1>> ==
                                 mtp::is_const<mtp::rm_ref<P2>> &&
                             mtp::same_as<typename F1::to_dyn, typename F2::to_dyn>)) {
                return false;
            }
        } else if constexpr (! mtp::same_as<T1, T2>) {
            return false;
        }
        return check_apis_quiet<Trait, ExpectedApi, ToCheck, Index + 1>();
    }
    return true;
}

template<typename Trait, typename A, typename ToCheck>
consteval bool check_trait_apis_quiet() {
    if constexpr (! mtp::has_trait_api<Trait>) {
        return true;
    } else if constexpr (! trait_apis_formable<Trait, A, ToCheck>) {
        return false;
    } else {
        return check_apis_quiet<Trait, TraitApi<Trait, A>, ToCheck>();
    }
}

template<typename Trait, typename A>
consteval auto select_trait_impl_kind() -> trait_impl_kind {
    if constexpr (mtp::same_as<A, dyn_tag>) {
        return trait_impl_kind::Dyn;
    } else if constexpr (mtp::is_direct_trait<Trait>) {
        return trait_impl_kind::Direct;
    } else if constexpr (has_external_trait_impl<Trait, A>) {
        return trait_impl_kind::External;
    } else if constexpr (has_in_class_trait_impl<Trait, A>) {
        return trait_impl_kind::InClass;
    } else {
        return trait_impl_kind::None;
    }
}

template<typename Trait, typename A, trait_impl_kind Kind>
struct trait_impl_source_for;

template<typename Trait, typename A>
struct trait_impl_source_for<Trait, A, trait_impl_kind::Dyn> {
    using api_owner              = dyn_tag;
    static constexpr auto kind   = trait_impl_kind::Dyn;
    static constexpr bool value  = true;
    static constexpr auto reason = trait_impl_failure_reason::None;
};

template<typename Trait, typename A>
struct trait_impl_source_for<Trait, A, trait_impl_kind::Direct> {
    using api_owner             = A;
    static constexpr auto kind  = trait_impl_kind::Direct;
    static constexpr bool value = check_trait_apis_quiet<Trait, A, A>();
    static constexpr auto reason =
        value ? trait_impl_failure_reason::None : trait_impl_failure_reason::DirectApiMismatch;
};

template<typename Trait, typename A>
struct trait_impl_source_for<Trait, A, trait_impl_kind::External> {
    using api_owner                 = Impl<Trait, A>;
    static constexpr auto kind      = trait_impl_kind::External;
    static constexpr bool available = mtp::drop<Impl<Trait, A>>;
    static constexpr bool api_ok = available && check_trait_apis_quiet<Trait, A, Impl<Trait, A>>();
    static constexpr bool value  = available && api_ok;
    static constexpr auto reason =
        value ? trait_impl_failure_reason::None
              : (available ? trait_impl_failure_reason::ExternalApiMismatch
                           : trait_impl_failure_reason::ExternalUnavailable);
};

template<typename Trait, typename A>
struct trait_impl_source_for<Trait, A, trait_impl_kind::InClass> {
    using api_owner             = A;
    static constexpr auto kind  = trait_impl_kind::InClass;
    static constexpr bool value = check_trait_apis_quiet<Trait, A, A>();
    static constexpr auto reason =
        value ? trait_impl_failure_reason::None : trait_impl_failure_reason::InClassApiMismatch;
};

template<typename Trait, typename A>
struct trait_impl_source_for<Trait, A, trait_impl_kind::None> {
    using api_owner              = void;
    static constexpr auto kind   = trait_impl_kind::None;
    static constexpr bool value  = false;
    static constexpr auto reason = trait_impl_failure_reason::NoImpl;
};

template<typename Trait, typename A>
struct trait_impl_source : trait_impl_source_for<Trait, A, select_trait_impl_kind<Trait, A>()> {};

template<typename Trait, typename A, trait_impl_failure_reason Reason>
struct trait_impl_failure;

template<typename Trait, typename A>
struct trait_impl_failure<Trait, A, trait_impl_failure_reason::NoImpl> {
    static_assert(dependent_false<Trait, A>,
                  "rstd::Impled failed: no external Impl or in-class DefaultInClass marker");
    static constexpr bool value = false;
};

template<typename Trait, typename A>
struct trait_impl_failure<Trait, A, trait_impl_failure_reason::ExternalUnavailable> {
    static_assert(dependent_false<Trait, A>,
                  "rstd::Impled failed: external Impl exists but is not usable");
    static constexpr bool value = false;
};

template<typename Trait, typename A>
struct trait_impl_failure<Trait, A, trait_impl_failure_reason::ExternalApiMismatch> {
    static_assert(dependent_false<Trait, A>,
                  "rstd::Impled failed: external Impl does not satisfy trait API");
    static constexpr bool value = false;
};

template<typename Trait, typename A>
struct trait_impl_failure<Trait, A, trait_impl_failure_reason::InClassApiMismatch> {
    static_assert(dependent_false<Trait, A>,
                  "rstd::Impled failed: in-class implementation does not satisfy trait API");
    static constexpr bool value = false;
};

template<typename Trait, typename A>
struct trait_impl_failure<Trait, A, trait_impl_failure_reason::DirectApiMismatch> {
    static_assert(dependent_false<Trait, A>,
                  "rstd::Impled failed: direct trait implementation does not satisfy trait API");
    static constexpr bool value = false;
};

template<typename Trait, typename A>
consteval bool check_trait_or_diagnose() {
    using source = trait_impl_source<Trait, A>;
    if constexpr (source::value) {
        return true;
    } else {
        return trait_impl_failure<Trait, A, source::reason>::value;
    }
}

/// Checks whether type A implements trait T, either directly or via Impl.
/// \tparam T The trait type.
/// \tparam A The type to check.
/// \return true if A implements T.
export template<typename T, typename A>
consteval bool check_trait() {
    return trait_impl_source<T, A>::value;
}

} // namespace mtp

/// Base class for Impl specializations, providing a pointer-based self accessor.
/// \tparam A The concrete type that the Impl operates on.
export template<typename A>
struct ImplBase : mtp::ImplWithPtr<A> {};

template<typename A>
struct ImplBase<default_tag<A>> : ImplBase<A> {};

// This is used as a base class for the user class, not for an external Impl.
template<typename A>
struct ImplBase<in_class_default_tag<A>> {
    template<typename, typename>
    friend struct Impl;

private:
    auto self() -> A& { return *static_cast<A*>(this); }
    auto self() const -> A const& { return *static_cast<A const*>(this); }
};

/// Checks whether type A implements all the given traits T.
/// \tparam A The type to check.
/// \tparam T The trait types to verify.
export template<typename A, typename... T>
concept Impled = (mtp::check_trait<T, mtp::rm_cvf<A>>() && ...);

/// Default methods inherited by a class that wants them as members.
/// \tparam Self The concrete type.
/// \tparam T The trait that owns the default methods.
export template<typename Self, typename T>
using DefaultInClass = Impl<T, in_class_default_tag<Self>>;

/// Default methods inherited by an Impl specialization.
/// \tparam T The trait that owns the default methods.
/// \tparam Self The concrete type.
export template<typename T, typename Self>
using DefaultInImpl = Impl<T, default_tag<Self>>;

/// Links a class's own methods as the trait implementation, delegating via in-class dispatch.
/// \tparam T The trait type.
/// \tparam A The concrete type whose methods satisfy the trait.
export template<typename T, typename A>
struct LinkClassMethod : mtp::ImplWithPtr<A>, mtp::rm_cv<T>::template Api<A, in_class_tag> {};

/// Links a class's required methods while inheriting the chosen Impl base.
/// \tparam T The trait type.
/// \tparam A The concrete type whose required methods satisfy the trait.
/// \tparam Base The Impl base that supplies self storage and provided methods.
export template<typename T, typename A, typename Base = ImplBase<A>>
struct LinkClassRequired : Base,
                           mtp::rm_cv<T>::template RequiredApi<A, LinkClassRequired<T, A, Base>> {
    template<typename P>
    constexpr LinkClassRequired(P* p) noexcept: Base { p } {}
};

/// Links class required methods and fills provided methods from the trait default Impl.
/// \tparam T The trait type.
/// \tparam A The concrete type whose required methods satisfy the trait.
export template<typename T, typename A>
using LinkClassRequiredWithDefault = LinkClassRequired<T, A, DefaultInImpl<T, A>>;

/// Dispatches a required trait method to the corresponding class member.
/// \tparam I The index of the method in the trait's required API function list.
/// \tparam TApi The required trait API type.
/// \param self Pointer to the required trait API object.
/// \param args Arguments forwarded to the class method.
/// \return The result of the dispatched method call.
export template<usize I, typename TApi, typename... Args>
    requires mtp::is_trait_api<mtp::rm_cv<TApi>>
[[gnu::always_inline]]
inline constexpr decltype(auto) trait_required_call(TApi* self, Args&&... args) {
    using TApi_    = mtp::rm_cv<TApi>;
    using Trait    = typename TApi_::Trait;
    using TClass   = typename mtp::TraitApiTraits<TApi_>::type;
    using Delegate = typename mtp::TraitApiTraits<TApi_>::delegate_type;

    constexpr const auto api { mtp::RequiredTraitApiHelper<Trait, TClass>::template get<I>() };
    auto                 impl_in_class = static_cast<mtp::follow_const_t<TApi, Delegate>*>(self);
    const auto           self_         = rstd::addressof(mtp::ImplHelper::get_self(impl_in_class));
    return (self_->*api)(rstd::forward<Args>(args)...);
}

/// Dispatches a trait method call to the appropriate Impl, handling static, dynamic, and in-class
/// dispatch.
/// \tparam I The index of the method in the trait's API function list.
/// \tparam TApi The trait API type (deduced from self).
/// \param self Pointer to the trait API object.
/// \param args Arguments forwarded to the trait method.
/// \return The result of the dispatched method call.
export template<usize I, typename TApi, typename... Args>
    requires mtp::is_trait_api<mtp::rm_cv<TApi>>
[[gnu::always_inline]]
inline constexpr decltype(auto) trait_call(TApi* self, Args&&... args) {
    using TApi_    = mtp::rm_cv<TApi>;
    using Trait    = typename TApi_::Trait;
    using TClass   = typename mtp::TraitApiTraits<TApi_>::type;
    using Delegate = typename mtp::TraitApiTraits<TApi_>::delegate_type;
    using TImpl    = Impl<Trait, TClass>;

    if constexpr (mtp::same_as<TClass, dyn_tag>) {
        // delegate for dyn
        auto dyn = static_cast<mtp::follow_const_t<TApi, ptr_::dyn_delegate<Trait>>*>(self);

        const auto* apis = mtp::DynHelper::get_apis(dyn);
        return rstd::get<I>(*apis)(mtp::DynHelper::get_self(dyn), rstd::forward<Args>(args)...);
    } else if constexpr (mtp::same_as<Delegate, in_class_tag>) {
        // delegate for LinkClassMethod
        // used for trait impl inherite Api
        constexpr const auto api { mtp::TraitApiHelper<Trait, TClass>::template get<I>() };

        auto impl_in_class =
            static_cast<mtp::follow_const_t<TApi, LinkClassMethod<Trait, TClass>>*>(self);

        const auto self_ = rstd::addressof(mtp::ImplHelper::get_self(impl_in_class));
        return (self_->*api)(rstd::forward<Args>(args)...);
    } else {
        // delegate for Impl
        // used for class inherite Api
        constexpr const auto api { mtp::TraitApiHelper<Trait, TImpl>::template get<I>() };

        TImpl const self_ { static_cast<mtp::follow_const_t<TApi, TClass>*>(self) };
        return (self_.*api)(rstd::forward<Args>(args)...);
    }
}

/// Dispatches a static (non-member) trait method call to the appropriate Impl.
/// \tparam I The index of the method in the trait's API function list.
/// \tparam TApi The trait API type.
/// \param args Arguments forwarded to the static trait method.
/// \return The result of the dispatched method call.
export template<usize I, typename TApi, typename... Args>
    requires mtp::is_trait_api<mtp::rm_cv<TApi>>
[[gnu::always_inline]]
inline constexpr decltype(auto) trait_static_call(Args&&... args) {
    using TApi_  = mtp::rm_cv<TApi>;
    using Trait  = typename TApi_::Trait;
    using TClass = typename mtp::TraitApiTraits<TApi_>::type;
    using TImpl  = Impl<Trait, TClass>;

    constexpr const auto api { mtp::TraitApiHelper<Trait, TImpl>::template get<I>() };
    return api(rstd::forward<Args>(args)...);
}

/// Casts an lvalue reference to a trait view, returning the Impl wrapper for trait T.
/// Only accepts lvalues to prevent stack-use-after-scope.
/// \tparam T The trait type to cast to.
/// \tparam A The concrete type (deduced).
/// \param t The object to view as the trait.
/// \return A trait Impl wrapper or a reference to t if the trait is direct.
export template<typename T, typename A>
[[gnu::always_inline]]
inline constexpr decltype(auto) as(A& t) noexcept {
    using class_t = mtp::rm_cvf<A>;
    using source  = mtp::trait_impl_source<T, class_t>;
    static_assert(mtp::check_trait_or_diagnose<T, class_t>());
    if constexpr (source::kind == mtp::trait_impl_kind::Direct ||
                  source::kind == mtp::trait_impl_kind::InClass) {
        return t;
    } else {
        using ret_t = mtp::follow_const_t<A, typename source::api_owner>;
        return ret_t { rstd::addressof(t) };
    }
}

} // namespace rstd
