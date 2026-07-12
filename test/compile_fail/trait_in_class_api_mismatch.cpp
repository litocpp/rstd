import rstd;

struct InClassTrait {
    template<typename Self, typename = void>
    struct Api {
        using Trait = InClassTrait;

        void apply() const { rstd::trait_call<0>(this); }
    };

    template<typename Self>
    using Funcs = rstd::TraitFuncs<&Self::apply>;
};

struct BadInClass {
    auto apply() const -> int { return 0; }
};

int main() {
    BadInClass value;
    rstd::as<InClassTrait>(value).apply();
}
