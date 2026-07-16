export module rstd.argparse:value_parser;
export import :error;

using namespace rstd::prelude;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

export namespace rstd::argparse
{

template<typename T>
struct ValueParser {
    template<typename Self, typename = void>
    struct Api {
        using Trait = ValueParser;

        auto parse(ref<OsStr> value) const -> Result<T, ValueError> {
            return trait_call<0>(this, value);
        }
    };

    template<typename U>
    using Funcs = TraitFuncs<&U::parse>;
};

class StringValueParser {
public:
    auto parse(ref<OsStr> value) const -> Result<String, ValueError> {
        auto text = value.to_str();
        if (text.is_none()) return Err(ValueError::InvalidUtf8());
        return Ok(String::make(*text));
    }
};

class OsStringValueParser {
public:
    auto parse(ref<OsStr> value) const -> Result<OsString, ValueError> {
        return Ok(OsString::from(value));
    }
};

template<typename T>
class FromStrValueParser {
public:
    auto parse(ref<OsStr> value) const -> Result<T, ValueError>
        requires Impled<T, str_::FromStr>
    {
        auto text = value.to_str();
        if (text.is_none()) return Err(ValueError::InvalidUtf8());

        auto result = rstd::from_str<T>(*text);
        if (result.is_ok()) return Ok(rstd::move(result).unwrap());

        using ParseError = typename Impl<str_::FromStr, T>::Err;
        if constexpr (Impled<ParseError, fmt::Display>) {
            return Err(ValueError::Message(rstd::format("{}", result.unwrap_err())));
        } else {
            return Err(ValueError::Message(String::make("failed to parse argument value")));
        }
    }
};

template<typename T, typename F>
class ParseWithValueParser {
    F function_;

public:
    explicit ParseWithValueParser(F function): function_(rstd::move(function)) {}

    auto parse(ref<OsStr> value) const -> Result<T, ValueError> {
        auto result = function_(value);
        using Error = typename decltype(result)::error_type;
        static_assert(mtp::same_as<typename decltype(result)::value_type, T>);
        if (result.is_ok()) return Ok(rstd::move(result).unwrap());
        if constexpr (mtp::same_as<Error, ValueError>) {
            return Err(rstd::move(result).unwrap_err());
        } else if constexpr (Impled<Error, fmt::Display>) {
            return Err(ValueError::Message(rstd::format("{}", result.unwrap_err())));
        } else {
            return Err(ValueError::Message(String::make("failed to parse argument value")));
        }
    }
};

template<typename T, typename P>
class ChoiceValueParser {
    P           parser_;
    Vec<T>      choices_;
    Vec<String> labels_;

public:
    ChoiceValueParser(P parser, Vec<T> choices)
        : parser_(rstd::move(parser)),
          choices_(rstd::move(choices)),
          labels_(Vec<String>::with_capacity(choices_.len())) {
        for (usize i = 0; i < choices_.len(); ++i) {
            labels_.push(rstd::format("{}", choices_[i]));
        }
    }

    auto parse(ref<OsStr> value) const -> Result<T, ValueError>
        requires requires(const T& left, const T& right) {
            { left == right } -> mtp::same_as<bool>;
        }
    {
        auto parsed = parser_.parse(value);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        auto result = rstd::move(parsed).unwrap();
        for (usize i = 0; i < choices_.len(); ++i) {
            if (result == choices_[i]) return Ok(rstd::move(result));
        }
        return Err(ValueError::Message(String::make("value is not one of the allowed choices")));
    }

    auto possible_values() const -> Vec<String> {
        auto values = Vec<String>::with_capacity(labels_.len());
        for (usize i = 0; i < labels_.len(); ++i) values.push(labels_[i].clone());
        return values;
    }
};

auto string_parser() -> StringValueParser {
    return {};
}
auto os_string_parser() -> OsStringValueParser {
    return {};
}

template<typename T>
auto from_str_parser() -> FromStrValueParser<T>
    requires Impled<T, str_::FromStr>
{
    return {};
}

template<typename T, typename F>
auto parse_with(F function) -> ParseWithValueParser<T, F> {
    return ParseWithValueParser<T, F> { rstd::move(function) };
}

template<typename T, typename P>
auto choice_parser(P parser, Vec<T> choices) -> ChoiceValueParser<T, P>
    requires mtp::same_as<T, String> || Impled<T, fmt::Display>
{
    return ChoiceValueParser<T, P> { rstd::move(parser), rstd::move(choices) };
}

} // namespace rstd::argparse

namespace rstd
{

template<typename T, typename P>
    requires requires(const P& parser, ref<ffi::OsStr> value) { parser.parse(value); } &&
             mtp::same_as<decltype(mtp::declval<const P&>().parse(mtp::declval<ref<ffi::OsStr>>())),
                          Result<T, argparse::ValueError>>
struct Impl<argparse::ValueParser<T>, P> : LinkClassMethod<argparse::ValueParser<T>, P> {};

} // namespace rstd

struct ErasedDefaultValue {
    template<typename Self, typename = void>
    struct Api {
        using Trait = ErasedDefaultValue;

        auto clone_value() const -> Box<dyn<rstd::any::Any>> { return rstd::trait_call<0>(this); }
        auto type_id() const noexcept -> rstd::any::TypeId { return rstd::trait_call<1>(this); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::clone_value, &T::type_id>;
};

template<typename T>
class ErasedDefaultValueAdapter {
    T value_;

public:
    explicit ErasedDefaultValueAdapter(T value): value_(rstd::move(value)) {}

    auto clone_value() const -> Box<dyn<rstd::any::Any>> {
        return Box<dyn<rstd::any::Any>>::make(as<rstd::clone::Clone>(value_).clone());
    }

    auto type_id() const noexcept -> rstd::any::TypeId { return rstd::any::TypeId::of<T>(); }
};

namespace rstd
{

template<typename T>
struct Impl<::ErasedDefaultValue, ::ErasedDefaultValueAdapter<T>>
    : LinkClassMethod<::ErasedDefaultValue, ::ErasedDefaultValueAdapter<T>> {};

} // namespace rstd

struct ErasedValueParser {
    template<typename Self, typename = void>
    struct Api {
        using Trait = ErasedValueParser;

        auto parse(ref<OsStr> value) const
            -> Result<Box<dyn<rstd::any::Any>>, rstd::argparse::ValueError> {
            return rstd::trait_call<0>(this, value);
        }
        auto parse_default(ref<OsStr> value) const
            -> Result<Box<dyn<ErasedDefaultValue>>, rstd::argparse::ValueError> {
            return rstd::trait_call<1>(this, value);
        }
        auto type_id() const noexcept -> rstd::any::TypeId { return rstd::trait_call<2>(this); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::parse, &T::parse_default, &T::type_id>;
};

template<typename T>
class ErasedValueParserAdapter {
    Box<dyn<rstd::argparse::ValueParser<T>>> parser_;

public:
    explicit ErasedValueParserAdapter(Box<dyn<rstd::argparse::ValueParser<T>>> parser)
        : parser_(rstd::move(parser)) {}

    auto parse(ref<OsStr> value) const
        -> Result<Box<dyn<rstd::any::Any>>, rstd::argparse::ValueError> {
        auto parsed = parser_->parse(value);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        return Ok(Box<dyn<rstd::any::Any>>::make(rstd::move(parsed).unwrap()));
    }

    auto parse_default(ref<OsStr> value) const
        -> Result<Box<dyn<ErasedDefaultValue>>, rstd::argparse::ValueError> {
        if constexpr (Impled<T, rstd::clone::Clone>) {
            auto parsed = parser_->parse(value);
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
            return Ok(Box<dyn<ErasedDefaultValue>>::make(
                ErasedDefaultValueAdapter<T> { rstd::move(parsed).unwrap() }));
        } else {
            return Err(rstd::argparse::ValueError::Message(
                String::make("default value type does not implement Clone")));
        }
    }

    auto type_id() const noexcept -> rstd::any::TypeId { return rstd::any::TypeId::of<T>(); }
};

namespace rstd
{

template<typename T>
struct Impl<::ErasedValueParser, ::ErasedValueParserAdapter<T>>
    : LinkClassMethod<::ErasedValueParser, ::ErasedValueParserAdapter<T>> {};

} // namespace rstd
