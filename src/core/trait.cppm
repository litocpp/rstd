export module rstd.core:trait;
export import :core;
export import :cppstd;
export import :meta;

namespace rstd
{
export template<auto... Api>
struct TraitCollect {};

template<typename T, typename A, typename Delegate = void>
struct TraitMeta;

namespace detail
{

template<typename T>
struct TraitCollectInner;

template<auto... Api>
struct TraitCollectInner<TraitCollect<Api...>> {
    template<template<class...> typename T>
    consteval static auto make() {
        return T { Api... };
    }

    template<usize I>
    consteval static auto get() {
        return meta::get_auto<I, Api...>();
    }

    consteval static auto size() { return sizeof...(Api); }

    template<usize I>
    using type_at = decltype(get<I>());
};

template<typename T>
struct ImplWithPtr {
    usizeptr ptr_;

    template<typename, typename, typename>
    friend struct rstd::TraitMeta;

    template<typename P>
    constexpr ImplWithPtr(P* p) noexcept: ptr_(rstd::bit_cast<usizeptr>(p)) {}

protected:
    constexpr auto self() noexcept -> T& { return *rstd::bit_cast<T*>(ptr_); }
    constexpr auto self() const noexcept -> T const& { return *rstd::bit_cast<T const*>(ptr_); }
};

template<typename T>
struct ApiInner;

template<template<typename, typename> class T, typename A, typename B>
struct ApiInner<T<A, B>> {
    using type          = A;
    using delegate_type = B;
};

template<typename Self, typename TApi, typename TApiB, usize Index = 0>
consteval bool check_apis() {
    if constexpr (Index == 0) {
        static_assert(TApi::size() == TApiB::size(), "Please implement all Trait api");
    }

    if constexpr (Index < TApi::size()) {
        using T1 = TApi::template type_at<Index>;
        using T2 = TApiB::template type_at<Index>;

        using F1 = meta::FuncTraits<T1>;
        using F2 = meta::FuncTraits<T2>;
        using P1 = typename F1::primary;
        using P2 = typename F2::primary;

        if constexpr (meta::same_as<Self, meta::remove_cv_t<meta::remove_reference_t<P1>>>) {
            // Is member func
            static_assert(meta::is_lvalue_reference_v<P1> == meta::is_lvalue_reference_v<P2> &&
                              meta::is_rvalue_reference_v<P1> == meta::is_rvalue_reference_v<P2> &&
                              meta::is_const_v<P1> == meta::is_const_v<P2> &&
                              meta::same_as<typename F1::to_dyn, typename F2::to_dyn>,
                          "Trait api not satisfy");
        } else {
            // not member func, full compare
            static_assert(meta::same_as<T1, T2>, "Trait api not satisfy");
        }
        return check_apis<Self, TApi, TApiB, Index + 1>();
    }
    return true;
}

template<typename... Api, template<class...> typename Tuple>
consteval auto to_dyn(Tuple<Api...>) {
    return Tuple { (typename meta::FuncTraits<Api>::to_dyn)(nullptr)... };
}

} // namespace detail

export template<typename T, typename A>
struct Impl;

enum class TraitDefaultPolicy
{
    InClass,
    Normal
};

export template<typename A>
struct ImplBase : detail::ImplWithPtr<A> {};

export template<typename T, TraitDefaultPolicy P = TraitDefaultPolicy::Normal>
struct default_tag {};

struct in_class_tag {};

struct dyn_tag {};
template<typename A>
struct ImplBase<default_tag<A, TraitDefaultPolicy::InClass>> {
    template<typename, typename>
    friend struct Impl;

private:
    auto self() -> A& { return *static_cast<A*>(this); }
    auto self() const -> A const& { return *static_cast<A const*>(this); }
};

template<typename A>
struct ImplBase<default_tag<A, TraitDefaultPolicy::Normal>> : ImplBase<A> {};

export template<typename A, typename... T>
concept Impled = (meta::destructible<Impl<T, meta::remove_cvref_t<A>>> && ...) ||
                 meta::same_as<meta::remove_cvref_t<A>, dyn_tag>;

export template<typename T>
concept IsTraitApi = requires() {
    typename T::Trait;
    typename detail::ApiInner<T>::type;
};

export template<typename T>
class Dyn;

export template<typename T, typename A, TraitDefaultPolicy P = TraitDefaultPolicy::Normal>
using ImplDefault = Impl<T, default_tag<A, P>>;

export template<typename T, typename A>
struct ImplInClass : detail::ImplWithPtr<A>, meta::remove_cv_t<T>::template Api<A, in_class_tag> {};

template<typename T, typename A, typename Delegate>
struct TraitMeta {
    using Api = typename T::template Api<A, Delegate>;
    template<typename F>
    using TCollect = detail::TraitCollectInner<typename T::template TCollect<F>>;

    template<template<class...> typename Tuple>
    using dyn_apis_t = decltype(detail::to_dyn(TCollect<Api>::template make<Tuple>()));

    template<typename TSelf, typename TDst>
    using call_cast_t = meta::conditional_t<meta::is_const_v<TSelf>, meta::add_const_t<TDst>, TDst>;

    template<usize I>
    static consteval auto get() {
        if constexpr (meta::same_as<A, dyn_tag>) {
            // delegate for dyn
            return TCollect<Api>::template get<I>();
        } else if constexpr (meta::same_as<Delegate, in_class_tag>) {
            // delegate for ImplInClass
            return TCollect<A>::template get<I>();
        } else {
            // delegate for Impl
            return TCollect<Impl<T, A>>::template get<I>();
        }
    }

    static consteval bool check() {
        using L1 = TCollect<Api>;

        if constexpr (L1::size() == 0) {
            return true;
        } else if constexpr (meta::same_as<A, dyn_tag>) {
            // delegate for dyn
            return true;
        } else if constexpr (meta::same_as<Delegate, in_class_tag>) {
            // delegate for ImplInClass
            using L2 = TCollect<A>;
            return detail::check_apis<Api, L1, L2>();
        } else {
            // delegate for Impl
            using L2 = TCollect<Impl<T, A>>;
            return detail::check_apis<Api, L1, L2>();
        }
    }

    static_assert(check());
    template<usize I, typename TSelf, typename... Args>
    [[gnu::always_inline]] inline static constexpr decltype(auto) call(TSelf* self,
                                                                       Args&&... args) {
        if constexpr (meta::same_as<A, dyn_tag>) {
            // delegate for dyn
            auto dyn = static_cast<call_cast_t<TSelf, Dyn<T>>*>(self);
            return rstd::get<I>(*dyn->apis)(dyn->self, rstd::forward<Args>(args)...);
        } else if constexpr (meta::same_as<Delegate, in_class_tag>) {
            // delegate for ImplInClass
            constexpr auto api { get<I>() };

            auto self_ = rstd::addressof(
                (static_cast<call_cast_t<TSelf, ImplInClass<T, A>>*>(self)->self()));
            return cppstd::invoke(api, self_, rstd::forward<Args>(args)...);
        } else {
            // delegate for Impl
            constexpr const auto api { get<I>() };

            Impl<T, A> self_ { static_cast<call_cast_t<TSelf, A>*>(self) };
            return cppstd::invoke(api, self_, rstd::forward<Args>(args)...);
        }
    }

    template<usize I, typename... Args>
    [[gnu::always_inline]] inline static constexpr decltype(auto) call_static(Args&&... args) {
        constexpr const auto api { get<I>() };
        return api(rstd::forward<Args>(args)...);
    }
};

export template<usize I, typename TApi, typename... Args>
    requires IsTraitApi<meta::remove_cv_t<TApi>>
[[gnu::always_inline]] inline constexpr decltype(auto) trait_call(TApi* self, Args&&... args) {
    using TApi_ = meta::remove_cv_t<TApi>;
    return TraitMeta<typename TApi_::Trait,
                     typename detail::ApiInner<TApi_>::type,
                     typename detail::ApiInner<TApi_>::delegate_type>::template call<I,
                                                                                     TApi>(
        self, rstd::forward<Args>(args)...);
}

export template<usize I, typename TApi, typename... Args>
    requires IsTraitApi<meta::remove_cv_t<TApi>>
[[gnu::always_inline]] inline constexpr decltype(auto) trait_static_call(Args&&... args) {
    using TApi_ = meta::remove_cv_t<TApi>;
    return TraitMeta<typename TApi_::Trait,
                     typename detail::ApiInner<TApi_>::type,
                     typename detail::ApiInner<TApi_>::delegate_type>::template call<I,
                                                                                     TApi>(
        rstd::forward<Args>(args)...);
}

template<typename T>
class Dyn : public meta::remove_cv_t<T>::template Api<dyn_tag> {
    using raw_t  = meta::remove_cv_t<T>;
    using M      = TraitMeta<raw_t, dyn_tag>;
    using ptr_t  = meta::conditional_t<meta::is_const_v<T>, const_voidp, voidp>;
    using apis_t = M::template dyn_apis_t<cppstd::tuple>;

    friend M;

    const apis_t* const apis;
    ptr_t               self;

    template<typename U>
    struct Convert {
        using UM = TraitMeta<raw_t, U>;
        template<usize I, typename Fn>
        struct Wrap;
        template<usize I, typename Ret, bool Ne, typename... Args>
        struct Wrap<I, Ret (*)(Args...) noexcept(Ne)> {
            consteval static auto get() { return UM::template get<I>(); }
        };
        template<usize I, typename Ret, bool Ne, typename... Args>
        struct Wrap<I, Ret (*)(voidp, Args...) noexcept(Ne)> {
            consteval static auto get() {
                return [](voidp p, Args... args) {
                    Impl<meta::remove_cv_t<T>, U> self { static_cast<const U*>(p) };
                    constexpr const auto          api { UM::template get<I>() };
                    return cppstd::invoke(api, self, rstd::forward<Args>(args)...);
                };
            }
        };

        template<usize... Is>
        consteval static auto convert(cppstd::index_sequence<Is...>) {
            return apis_t { (Wrap<Is, cppstd::tuple_element_t<Is, apis_t>>::get())... };
        }

        constexpr static apis_t apis { convert(
            cppstd::make_index_sequence<cppstd::tuple_size_v<apis_t>> {}) };
    };

public:
    template<typename U>
    constexpr Dyn(U* p) noexcept
        : apis(&Convert<meta::remove_cv_t<U>>::apis), self(static_cast<ptr_t>(p)) {}

    template<typename U>
    constexpr Dyn(U& p) noexcept: Dyn(&p) {}

    template<typename U>
    constexpr static auto make(U* p) noexcept -> Dyn {
        return { p };
    }

    Dyn(const Dyn&) noexcept = default;
    Dyn(Dyn&&) noexcept      = default;

    Dyn& operator=(const Dyn&) noexcept = default;
    Dyn& operator=(Dyn&&) noexcept      = default;
};

export template<typename T, typename A>
constexpr auto make_dyn(A* t) noexcept {
    using dyn_t = meta::conditional_t<meta::is_const_v<A>, Dyn<const T>, Dyn<T>>;
    return dyn_t::make(t);
}

export template<typename T, typename A>
constexpr auto make_dyn(A& t) noexcept {
    return make_dyn<T, A>(addressof(t));
}


///
/// @brief as a triat
// only accept lvalue for no stack-use-after-scope
// requires Impled<T, meta::remove_cvref_t<A>>, maybe used in impldef which not satisfy
export template<typename T, typename A>
[[gnu::always_inline]] inline constexpr auto as(A& t) noexcept {
    using impl_t = Impl<T, meta::remove_cvref_t<A>>;
    using ret_t  = meta::conditional_t<meta::is_const_v<A>, meta::add_const_t<impl_t>, impl_t>;
    return ret_t { rstd::addressof(t) };
}

export template<typename Self, typename... Traits>
struct WithTrait : Traits::template Api<Self>... {
    constexpr bool operator==(const WithTrait) const { return true; }
};

export template<typename Self, typename... Traits>
struct WithTraitDefault : ImplDefault<Traits, Self, TraitDefaultPolicy::InClass>... {
    constexpr bool operator==(const WithTraitDefault) const { return true; }
};

export template<typename Self, typename... Traits>
struct MayWithTrait
    : meta::conditional_t<Impled<Traits, Self>, typename Traits::template Api<Self>, Empty>... {
    constexpr bool operator==(const MayWithTrait) const { return true; }
};

} // namespace rstd