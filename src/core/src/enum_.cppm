export module rstd.core:enum_;
export import :choice;

export namespace rstd
{
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
