import rstd;

struct DirectBadTrait {
    static constexpr bool direct { true };

    template<typename Self, typename = void>
    struct Api {
        using Trait = DirectBadTrait;
        auto value() -> int { return rstd::trait_call<0>(this); }
    };

    template<typename Self>
    using Funcs = rstd::TraitFuncs<&Self::value>;
};

struct BadDirect {
    void value() {}
};

int main() {
    BadDirect value;
    (void)rstd::as<DirectBadTrait>(value).value();
}
