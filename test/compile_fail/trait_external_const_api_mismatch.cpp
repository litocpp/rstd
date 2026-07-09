import rstd;

struct MutOnlyTrait {
    template<typename Self, typename = void>
    struct Api {
        using Trait = MutOnlyTrait;
        auto touch() -> int { return rstd::trait_call<0>(this); }
    };

    template<typename Self>
    using Funcs = rstd::TraitFuncs<&Self::touch>;
};

struct ConstExternal {};

template<>
struct rstd::Impl<MutOnlyTrait, ConstExternal> : rstd::ImplBase<ConstExternal> {
    auto touch() const -> int { return 1; }
};

int main() {
    ConstExternal value;
    (void)rstd::as<MutOnlyTrait>(value).touch();
}
