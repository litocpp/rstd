export module rstd.macro;

import std;
import pmacro;
import :derive;
import :enum_;

using rstd::macro::Output;
using rstd::macro::Trait;
using rstd::macro::derive;

[[pmacro::define("rstd")]]
auto Clone(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return derive(input, context, Trait::Clone);
}

[[pmacro::define("rstd")]]
auto Debug(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return derive(input, context, Trait::Debug);
}

[[pmacro::define("rstd")]]
auto Display(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return derive(input, context, Trait::Display);
}

[[pmacro::define("rstd")]]
auto Error(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return derive(input, context, Trait::Error);
}

[[pmacro::define("rstd")]]
auto FromStr(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return derive(input, context, Trait::FromStr);
}

[[pmacro::define("rstd")]]
auto EnumIter(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return derive(input, context, Trait::EnumIter);
}

[[pmacro::define("rstd")]]
auto TryFrom(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return derive(input, context, Trait::TryFrom);
}

[[pmacro::define("rstd")]]
auto Enum(pmacro::FunctionInput input, pmacro::Context& context) noexcept -> Output {
    return rstd::macro::enum_declaration(input, context);
}

[[pmacro::define("rstd")]]
auto EnumCheck(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return rstd::macro::enum_check(input, context);
}
