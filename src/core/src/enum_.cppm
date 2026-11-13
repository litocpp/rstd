export module rstd.core:enum_;
export import :choice;

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
