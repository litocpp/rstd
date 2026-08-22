export module rstd.parse.alloc:rule;
export import :error;

using namespace rstd::prelude;
using ::alloc::vec::Vec;

export namespace rstd::parse
{

class RuntimeAdapter {
    SourceId source_;

public:
    using error_type = ParseError;

    explicit RuntimeAdapter(SourceId source): source_(rstd::move(source)) {}

    auto source() const noexcept [[clang::lifetimebound]] -> const SourceId& { return source_; }

    auto expected(RuleId rule, Span span) const -> ParseError {
        return ParseError::expected(source_.clone(), span, rule);
    }

    auto stalled(RuleId rule, Span span) const -> ParseError {
        return ParseError::stalled(source_.clone(), span, rule);
    }

    auto capacity(RuleId rule, Span span) const -> ParseError {
        return ParseError::capacity(source_.clone(), span, rule);
    }
};

struct VecCollectionPolicy {
    template<typename T>
    using collection_type = Vec<T>;

    template<typename T>
    auto make() const -> collection_type<T> {
        return collection_type<T>::make();
    }

    template<typename T>
    auto push(collection_type<T>& collection, T value) const -> bool {
        collection.push(rstd::move(value));
        return true;
    }
};

template<typename Rule>
auto repeat(RuleId id, Rule rule) -> RepeatRule<Rule, VecCollectionPolicy, false> {
    return repeat(id, rstd::move(rule), VecCollectionPolicy {});
}

template<typename Rule>
auto repeat_one(RuleId id, Rule rule) -> RepeatRule<Rule, VecCollectionPolicy, true> {
    return repeat_one(id, rstd::move(rule), VecCollectionPolicy {});
}

template<typename Rule, typename Separator>
auto separated(RuleId id, Rule rule, Separator separator)
    -> SeparatedRule<Rule, Separator, VecCollectionPolicy, false> {
    return separated(id, rstd::move(rule), rstd::move(separator), VecCollectionPolicy {});
}

template<typename Rule, typename Separator>
auto separated_one(RuleId id, Rule rule, Separator separator)
    -> SeparatedRule<Rule, Separator, VecCollectionPolicy, true> {
    return separated_one(id, rstd::move(rule), rstd::move(separator), VecCollectionPolicy {});
}

template<typename T, typename Observer, typename Rule>
auto parse(Input<T> input, SourceId source, Rule& rule, Observer& observer)
    -> Match<typename Rule::value_type, ParseError> {
    RuntimeAdapter                      adapter(rstd::move(source));
    Driver<T, RuntimeAdapter, Observer> driver(input, adapter, observer);
    return driver.apply(rule);
}

template<typename T, typename Rule>
auto parse(Input<T> input, SourceId source, Rule& rule)
    -> Match<typename Rule::value_type, ParseError> {
    NoopObserver observer;
    return parse(input, rstd::move(source), rule, observer);
}

} // namespace rstd::parse
