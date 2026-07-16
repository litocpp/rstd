export module rstd.core:any;
export import :ptr;

using namespace rstd::prelude;

export namespace rstd::any
{

class TypeId {
    template<typename T>
    struct Token {
        static inline const u8 value = 0;
    };

    const void* value_;

    explicit constexpr TypeId(const void* value) noexcept: value_(value) {}

public:
    template<typename T>
    static auto of() noexcept -> TypeId {
        using U = mtp::rm_cvf<T>;
        return TypeId { rstd::addressof(Token<U>::value) };
    }

    friend constexpr auto operator==(TypeId, TypeId) noexcept -> bool = default;
};

struct Any {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Any;

        auto type_id() const noexcept -> TypeId { return trait_call<0>(this); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::type_id>;
};

template<typename T>
auto is(ref<dyn<Any>> value) noexcept -> bool {
    return value->type_id() == TypeId::of<T>();
}

template<typename T>
auto is(mut_ref<dyn<Any>> value) noexcept -> bool {
    return is<T>(value.as_ref());
}

template<typename T>
auto downcast_ref(ref<dyn<Any>> value) noexcept -> Option<ref<T>> {
    if (! is<T>(value)) return None();
    auto result = ref<T>::from_raw_parts(static_cast<const T*>(value.as_raw_ptr()));
    return Some(rstd::move(result));
}

template<typename T>
auto downcast_mut(mut_ref<dyn<Any>> value) noexcept -> Option<mut_ref<T>> {
    if (! is<T>(value)) return None();
    auto result = mut_ref<T>::from_raw_parts(static_cast<T*>(value.as_raw_ptr()));
    return Some(rstd::move(result));
}

} // namespace rstd::any

namespace rstd
{

template<typename T>
    requires mtp::complete<mtp::rm_cvf<T>>
struct Impl<any::Any, T> : ImplBase<T> {
    auto type_id() const noexcept -> any::TypeId { return any::TypeId::of<T>(); }
};

} // namespace rstd
