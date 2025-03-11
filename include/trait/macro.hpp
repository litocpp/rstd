#pragma once

#define TRAIT(name, ...)                       \
private:                                       \
    using M = rstd::TraitMeta<name, T>;        \
    friend M;                                  \
    template<typename F>                       \
    static consteval auto collect() {          \
        return rstd::TraitApi { __VA_ARGS__ }; \
    }