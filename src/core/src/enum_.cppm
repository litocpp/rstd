export module rstd.core:enum_;
export import :choice;
export import :str.str;

export namespace rstd
{
struct EnumIter {
    template<typename Self, typename = void>
    struct Api {
        using Trait = EnumIter;
        static auto iter() { return Impl<EnumIter, Self>::iter(); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::iter>;
};

template<typename T>
    requires Impled<T, EnumIter>
auto enum_iter() {
    return EnumIter::Api<T>::iter();
}

struct VariantDescription final {};

enum class VariantKind
{
    Unit,
    Tuple,
    Record,
    External
};

template<typename T>
struct PayloadMemberType;
template<typename T, typename V>
struct PayloadMemberType<V T::*> {
    using type = V;
};

template<str_::fixed_string Name, auto Pointer>
struct PayloadField {
    using type                         = typename PayloadMemberType<decltype(Pointer)>::type;
    static constexpr auto pointer      = Pointer;
    static constexpr auto name_literal = Name;

    static constexpr auto name() -> ref<str> { return literals::operator""_str < Name>(); }
    template<typename T>
    static constexpr auto get(T&& value) noexcept -> decltype(auto) {
        return rstd::forward<T>(value).*Pointer;
    }
};

template<typename T, VariantKind Kind, str_::fixed_string Name, typename... Fields>
struct VariantPayload {
    using type                           = T;
    static constexpr auto   kind         = Kind;
    static constexpr size_t field_count  = sizeof...(Fields);
    static constexpr auto   name_literal = Name;
    static_assert(Kind != VariantKind::Unit || field_count == 0,
                  "unit payloads cannot have fields");

    static constexpr auto name() -> ref<str> { return literals::operator""_str < Name>(); }
    template<size_t I>
    using field_at = mtp::tuple_element<I, tuple<Fields...>>;
    template<typename F>
    static constexpr void fields(F&& function) {
        (function.template operator()<Fields>(), ...);
    }
    template<typename... Args>
        requires requires(Args&&... args) { T { rstd::forward<Args>(args)... }; }
    static constexpr auto
    make(Args&&... args) noexcept(noexcept(T { rstd::forward<Args>(args)... })) -> T {
        return T { rstd::forward<Args>(args)... };
    }
};

template<typename T>
concept DescribedPayload = requires { rstd_payload_definition(static_cast<T*>(nullptr)); };
template<DescribedPayload T>
using PayloadDefinition = decltype(rstd_payload_definition(static_cast<T*>(nullptr)));

template<typename... Entries>
struct VariantList {
    template<typename F>
    static constexpr void for_each(F&& function) {
        (function.template operator()<Entries>(), ...);
    }
};

template<typename T>
    requires requires { typename T::Variants; }
struct Impl<VariantDescription, T> {
    template<typename F>
    static constexpr void variants(F&& function) {
        T::Variants::for_each(rstd::forward<F>(function));
    }
};
} // namespace rstd
