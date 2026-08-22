export module rstd.parse.core:rule;
export import :collection;

using namespace rstd::prelude;

export namespace rstd::parse
{

enum class ErrorKind : rstd::uint8_t
{
    Expected,
    Stalled,
    Capacity,
};

struct BasicError {
    ErrorKind kind;
    Span      span;
};

struct BasicAdapter {
    using error_type = BasicError;

    constexpr auto expected(RuleId, Span span) const noexcept -> BasicError {
        return BasicError { .kind = ErrorKind::Expected, .span = span };
    }

    constexpr auto stalled(RuleId, Span span) const noexcept -> BasicError {
        return BasicError { .kind = ErrorKind::Stalled, .span = span };
    }

    constexpr auto capacity(RuleId, Span span) const noexcept -> BasicError {
        return BasicError { .kind = ErrorKind::Capacity, .span = span };
    }
};

template<typename T, typename Error = BasicError>
using Match = Result<Option<T>, Error>;

struct NoopObserver {
    constexpr void start(RuleId, usize) noexcept {}
    constexpr void success(RuleId, Span) noexcept {}
    constexpr void mismatch(RuleId, usize) noexcept {}

    template<typename Error>
    constexpr void error(RuleId, const Error&) noexcept {}
};

template<typename T, typename Adapter, typename Observer>
class Driver {
    Cursor<T> cursor_;
    Adapter&  adapter_;
    Observer& observer_;

public:
    using error_type = typename Adapter::error_type;

    constexpr Driver(Input<T> input, Adapter& adapter, Observer& observer)
        : cursor_(input), adapter_(adapter), observer_(observer) {}

    constexpr auto cursor() noexcept [[clang::lifetimebound]] -> Cursor<T>& { return cursor_; }
    constexpr auto cursor() const noexcept [[clang::lifetimebound]] -> const Cursor<T>& {
        return cursor_;
    }

    constexpr auto adapter() noexcept [[clang::lifetimebound]] -> Adapter& { return adapter_; }
    constexpr auto adapter() const noexcept [[clang::lifetimebound]] -> const Adapter& {
        return adapter_;
    }

    template<typename Rule>
    constexpr auto apply(Rule& rule) -> Match<typename Rule::value_type, error_type> {
        auto start = cursor_.checkpoint();
        observer_.start(rule.id(), cursor_.position());
        auto result = rule.match(*this);
        if (result.is_err()) {
            auto borrowed = result.as_ref();
            observer_.error(rule.id(), borrowed.unwrap_err());
            return result;
        }
        if (result->is_none()) {
            cursor_.rewind(start);
            observer_.mismatch(rule.id(), cursor_.position());
            return result;
        }
        observer_.success(rule.id(), cursor_.span_from(start));
        return result;
    }

    constexpr auto expected(RuleId rule) const -> error_type {
        auto offset = cursor_.furthest_position();
        return adapter_.expected(rule, Span { .begin = offset, .end = offset });
    }

    constexpr auto stalled(RuleId rule, usize offset) const -> error_type {
        return adapter_.stalled(rule, Span { .begin = offset, .end = offset });
    }

    constexpr auto capacity(RuleId rule, usize offset) const -> error_type {
        return adapter_.capacity(rule, Span { .begin = offset, .end = offset });
    }
};

template<typename Predicate>
class AtomicRule {
    RuleId    id_;
    Predicate predicate_;

public:
    using value_type               = Span;
    static constexpr bool nullable = false;

    constexpr AtomicRule(RuleId id, Predicate predicate)
        : id_(id), predicate_(rstd::move(predicate)) {}

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<Span, typename Adapter::error_type> {
        return Ok(consume_if(driver.cursor(), predicate_));
    }
};

template<typename Predicate>
constexpr auto atomic(RuleId id, Predicate predicate) -> AtomicRule<Predicate> {
    return AtomicRule<Predicate>(id, rstd::move(predicate));
}

class TextRule {
    RuleId   id_;
    ref<str> text_;

public:
    using value_type               = Span;
    static constexpr bool nullable = false;

    constexpr TextRule(RuleId id, ref<str> text) noexcept: id_(id), text_(text) {
        if (text.is_empty()) rstd::panic("text parse rule cannot be empty");
    }

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename Adapter, typename Observer>
    constexpr auto match(Driver<u8, Adapter, Observer>& driver)
        -> Match<Span, typename Adapter::error_type> {
        return Ok(consume_literal(driver.cursor(), text_));
    }
};

inline auto text(RuleId id, ref<str> value) -> TextRule {
    return TextRule(id, value);
}

class EndRule {
    RuleId id_;

public:
    using value_type               = Span;
    static constexpr bool nullable = true;

    constexpr explicit EndRule(RuleId id) noexcept: id_(id) {}
    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<Span, typename Adapter::error_type> {
        auto offset = driver.cursor().position();
        if (! driver.cursor().is_eof()) return Ok<Option<Span>>(None());
        return Ok(Some(Span { .begin = offset, .end = offset }));
    }
};

inline auto end(RuleId id) -> EndRule {
    return EndRule(id);
}

template<typename Left, typename Right>
class SequenceRule {
    RuleId id_;
    Left   left_;
    Right  right_;

public:
    using value_type               = tuple<typename Left::value_type, typename Right::value_type>;
    static constexpr bool nullable = Left::nullable && Right::nullable;

    constexpr SequenceRule(RuleId id, Left left, Right right)
        : id_(id), left_(rstd::move(left)), right_(rstd::move(right)) {}

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<value_type, typename Adapter::error_type> {
        auto left = driver.apply(left_);
        if (left.is_err()) return Err(rstd::move(left).unwrap_err_unchecked());
        auto left_value = rstd::move(left).unwrap_unchecked();
        if (left_value.is_none()) return Ok<Option<value_type>>(None());

        auto right = driver.apply(right_);
        if (right.is_err()) return Err(rstd::move(right).unwrap_err_unchecked());
        auto right_value = rstd::move(right).unwrap_unchecked();
        if (right_value.is_none()) return Ok<Option<value_type>>(None());

        return Ok(Some(value_type(rstd::move(left_value).unwrap_unchecked(),
                                  rstd::move(right_value).unwrap_unchecked())));
    }
};

template<typename Left, typename Right>
constexpr auto seq(RuleId id, Left left, Right right) -> SequenceRule<Left, Right> {
    return SequenceRule<Left, Right>(id, rstd::move(left), rstd::move(right));
}

template<typename Open, typename Rule, typename Close>
class DelimitedRule {
    RuleId id_;
    Open   open_;
    Rule   rule_;
    Close  close_;

public:
    using value_type               = typename Rule::value_type;
    static constexpr bool nullable = Open::nullable && Rule::nullable && Close::nullable;

    constexpr DelimitedRule(RuleId id, Open open, Rule rule, Close close)
        : id_(id), open_(rstd::move(open)), rule_(rstd::move(rule)), close_(rstd::move(close)) {}

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<value_type, typename Adapter::error_type> {
        auto open = driver.apply(open_);
        if (open.is_err()) return Err(rstd::move(open).unwrap_err_unchecked());
        if (open->is_none()) return Ok<Option<value_type>>(None());

        auto value = driver.apply(rule_);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        auto optional = rstd::move(value).unwrap_unchecked();
        if (optional.is_none()) return Ok<Option<value_type>>(None());

        auto close = driver.apply(close_);
        if (close.is_err()) return Err(rstd::move(close).unwrap_err_unchecked());
        if (close->is_none()) return Ok<Option<value_type>>(None());
        return Ok(Some(rstd::move(optional).unwrap_unchecked()));
    }
};

template<typename Open, typename Rule, typename Close>
constexpr auto delimited(RuleId id, Open open, Rule rule, Close close)
    -> DelimitedRule<Open, Rule, Close> {
    return DelimitedRule<Open, Rule, Close>(
        id, rstd::move(open), rstd::move(rule), rstd::move(close));
}

template<typename Left, typename Right>
    requires mtp::same_as<typename Left::value_type, typename Right::value_type>
class ChoiceRule {
    RuleId id_;
    Left   left_;
    Right  right_;

public:
    using value_type               = typename Left::value_type;
    static constexpr bool nullable = Left::nullable || Right::nullable;

    constexpr ChoiceRule(RuleId id, Left left, Right right)
        : id_(id), left_(rstd::move(left)), right_(rstd::move(right)) {}

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<value_type, typename Adapter::error_type> {
        auto left = driver.apply(left_);
        if (left.is_err() || left->is_some()) return left;
        return driver.apply(right_);
    }
};

template<typename Left, typename Right>
constexpr auto choice(RuleId id, Left left, Right right) -> ChoiceRule<Left, Right> {
    return ChoiceRule<Left, Right>(id, rstd::move(left), rstd::move(right));
}

template<typename Rule>
class OptionalRule {
    RuleId id_;
    Rule   rule_;

public:
    using value_type               = Option<typename Rule::value_type>;
    static constexpr bool nullable = true;

    constexpr OptionalRule(RuleId id, Rule rule): id_(id), rule_(rstd::move(rule)) {}
    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<value_type, typename Adapter::error_type> {
        auto value = driver.apply(rule_);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        auto optional = rstd::move(value).unwrap_unchecked();
        if (optional.is_none()) return Ok(Some(value_type(None())));
        return Ok(Some(value_type(Some(rstd::move(optional).unwrap_unchecked()))));
    }
};

template<typename Rule>
constexpr auto optional(RuleId id, Rule rule) -> OptionalRule<Rule> {
    return OptionalRule<Rule>(id, rstd::move(rule));
}

template<typename Rule, typename CollectionPolicy, bool RequireOne>
    requires CollectionPolicyFor<CollectionPolicy, typename Rule::value_type>
class RepeatRule {
    RuleId           id_;
    Rule             rule_;
    CollectionPolicy collection_policy_;

public:
    using value_type               = CollectionOf<CollectionPolicy, typename Rule::value_type>;
    static constexpr bool nullable = ! RequireOne;

    constexpr RepeatRule(RuleId id, Rule rule, CollectionPolicy collection_policy)
        : id_(id), rule_(rstd::move(rule)), collection_policy_(rstd::move(collection_policy)) {
        static_assert(! Rule::nullable, "repeat requires a non-nullable rule");
    }

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<value_type, typename Adapter::error_type> {
        auto values = collection_policy_.template make<typename Rule::value_type>();
        for (;;) {
            auto before = driver.cursor().position();
            auto value  = driver.apply(rule_);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
            auto optional = rstd::move(value).unwrap_unchecked();
            if (optional.is_none()) break;
            if (driver.cursor().position() == before) {
                return Err(driver.stalled(rule_.id(), before));
            }
            if (! collection_policy_.push(values, rstd::move(optional).unwrap_unchecked())) {
                return Err(driver.capacity(id_, driver.cursor().position()));
            }
        }
        if constexpr (RequireOne) {
            if (values.is_empty()) return Ok<Option<value_type>>(None());
        }
        return Ok(Some(rstd::move(values)));
    }
};

template<typename Rule, typename CollectionPolicy>
constexpr auto repeat(RuleId id, Rule rule, CollectionPolicy policy)
    -> RepeatRule<Rule, CollectionPolicy, false> {
    return RepeatRule<Rule, CollectionPolicy, false>(id, rstd::move(rule), rstd::move(policy));
}

template<typename Rule, typename CollectionPolicy>
constexpr auto repeat_one(RuleId id, Rule rule, CollectionPolicy policy)
    -> RepeatRule<Rule, CollectionPolicy, true> {
    return RepeatRule<Rule, CollectionPolicy, true>(id, rstd::move(rule), rstd::move(policy));
}

template<rstd::size_t Capacity, typename Rule>
constexpr auto repeat_fixed(RuleId id, Rule rule)
    -> RepeatRule<Rule, FixedCollectionPolicy<Capacity>, false> {
    return repeat(id, rstd::move(rule), FixedCollectionPolicy<Capacity> {});
}

template<rstd::size_t Capacity, typename Rule>
constexpr auto repeat_one_fixed(RuleId id, Rule rule)
    -> RepeatRule<Rule, FixedCollectionPolicy<Capacity>, true> {
    return repeat_one(id, rstd::move(rule), FixedCollectionPolicy<Capacity> {});
}

template<typename Rule, typename Separator, typename CollectionPolicy, bool RequireOne>
    requires CollectionPolicyFor<CollectionPolicy, typename Rule::value_type>
class SeparatedRule {
    RuleId           id_;
    Rule             rule_;
    Separator        separator_;
    CollectionPolicy collection_policy_;

public:
    using value_type               = CollectionOf<CollectionPolicy, typename Rule::value_type>;
    static constexpr bool nullable = ! RequireOne;

    constexpr SeparatedRule(RuleId           id,
                            Rule             rule,
                            Separator        separator,
                            CollectionPolicy collection_policy)
        : id_(id),
          rule_(rstd::move(rule)),
          separator_(rstd::move(separator)),
          collection_policy_(rstd::move(collection_policy)) {
        static_assert(! Rule::nullable, "separated requires a non-nullable value rule");
        static_assert(! Separator::nullable, "separated requires a non-nullable separator");
    }

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<value_type, typename Adapter::error_type> {
        auto values = collection_policy_.template make<typename Rule::value_type>();
        auto first  = driver.apply(rule_);
        if (first.is_err()) return Err(rstd::move(first).unwrap_err_unchecked());
        auto first_value = rstd::move(first).unwrap_unchecked();
        if (first_value.is_none()) {
            if constexpr (RequireOne) return Ok<Option<value_type>>(None());
            return Ok(Some(rstd::move(values)));
        }
        if (! collection_policy_.push(values, rstd::move(first_value).unwrap_unchecked())) {
            return Err(driver.capacity(id_, driver.cursor().position()));
        }

        for (;;) {
            auto before_separator = driver.cursor().checkpoint();
            auto separator        = driver.apply(separator_);
            if (separator.is_err()) return Err(rstd::move(separator).unwrap_err_unchecked());
            if (separator->is_none()) break;

            auto value = driver.apply(rule_);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
            auto optional = rstd::move(value).unwrap_unchecked();
            if (optional.is_none()) {
                driver.cursor().rewind(before_separator);
                break;
            }
            if (! collection_policy_.push(values, rstd::move(optional).unwrap_unchecked())) {
                return Err(driver.capacity(id_, driver.cursor().position()));
            }
        }
        return Ok(Some(rstd::move(values)));
    }
};

template<typename Rule, typename Separator, typename CollectionPolicy>
constexpr auto separated(RuleId id, Rule rule, Separator separator, CollectionPolicy policy)
    -> SeparatedRule<Rule, Separator, CollectionPolicy, false> {
    return SeparatedRule<Rule, Separator, CollectionPolicy, false>(
        id, rstd::move(rule), rstd::move(separator), rstd::move(policy));
}

template<typename Rule, typename Separator, typename CollectionPolicy>
constexpr auto separated_one(RuleId id, Rule rule, Separator separator, CollectionPolicy policy)
    -> SeparatedRule<Rule, Separator, CollectionPolicy, true> {
    return SeparatedRule<Rule, Separator, CollectionPolicy, true>(
        id, rstd::move(rule), rstd::move(separator), rstd::move(policy));
}

template<rstd::size_t Capacity, typename Rule, typename Separator>
constexpr auto separated_fixed(RuleId id, Rule rule, Separator separator)
    -> SeparatedRule<Rule, Separator, FixedCollectionPolicy<Capacity>, false> {
    return separated(
        id, rstd::move(rule), rstd::move(separator), FixedCollectionPolicy<Capacity> {});
}

template<rstd::size_t Capacity, typename Rule, typename Separator>
constexpr auto separated_one_fixed(RuleId id, Rule rule, Separator separator)
    -> SeparatedRule<Rule, Separator, FixedCollectionPolicy<Capacity>, true> {
    return separated_one(
        id, rstd::move(rule), rstd::move(separator), FixedCollectionPolicy<Capacity> {});
}

template<typename Rule, typename Function>
class MapRule {
    RuleId   id_;
    Rule     rule_;
    Function function_;

public:
    using value_type               = mtp::invoke_result_t<Function, typename Rule::value_type>;
    static constexpr bool nullable = Rule::nullable;

    constexpr MapRule(RuleId id, Rule rule, Function function)
        : id_(id), rule_(rstd::move(rule)), function_(rstd::move(function)) {}

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<value_type, typename Adapter::error_type> {
        auto value = driver.apply(rule_);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        auto optional = rstd::move(value).unwrap_unchecked();
        if (optional.is_none()) return Ok<Option<value_type>>(None());
        return Ok(Some(function_(rstd::move(optional).unwrap_unchecked())));
    }
};

template<typename Rule, typename Function>
constexpr auto map(RuleId id, Rule rule, Function function) -> MapRule<Rule, Function> {
    return MapRule<Rule, Function>(id, rstd::move(rule), rstd::move(function));
}

template<typename Rule>
class CommitRule {
    RuleId id_;
    Rule   rule_;

public:
    using value_type               = typename Rule::value_type;
    static constexpr bool nullable = Rule::nullable;

    constexpr CommitRule(RuleId id, Rule rule): id_(id), rule_(rstd::move(rule)) {}
    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<value_type, typename Adapter::error_type> {
        auto value = driver.apply(rule_);
        if (value.is_err()) return value;
        if (value->is_none()) return Err(driver.expected(rule_.id()));
        return value;
    }
};

template<typename Rule>
constexpr auto commit(RuleId id, Rule rule) -> CommitRule<Rule> {
    return CommitRule<Rule>(id, rstd::move(rule));
}

template<typename Rule>
class LookaheadRule {
    RuleId id_;
    Rule   rule_;

public:
    using value_type               = typename Rule::value_type;
    static constexpr bool nullable = true;

    constexpr LookaheadRule(RuleId id, Rule rule): id_(id), rule_(rstd::move(rule)) {}
    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<value_type, typename Adapter::error_type> {
        auto checkpoint = driver.cursor().checkpoint();
        auto value      = driver.apply(rule_);
        driver.cursor().rewind(checkpoint);
        return value;
    }
};

template<typename Rule>
constexpr auto lookahead(RuleId id, Rule rule) -> LookaheadRule<Rule> {
    return LookaheadRule<Rule>(id, rstd::move(rule));
}

template<typename Rule>
class NegativeLookaheadRule {
    RuleId id_;
    Rule   rule_;

public:
    using value_type               = Span;
    static constexpr bool nullable = true;

    constexpr NegativeLookaheadRule(RuleId id, Rule rule): id_(id), rule_(rstd::move(rule)) {}
    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Adapter, typename Observer>
    constexpr auto match(Driver<T, Adapter, Observer>& driver)
        -> Match<Span, typename Adapter::error_type> {
        auto checkpoint = driver.cursor().checkpoint();
        auto start      = driver.cursor().position();
        auto value      = driver.apply(rule_);
        driver.cursor().rewind(checkpoint);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        if (value->is_some()) return Ok<Option<Span>>(None());
        return Ok(Some(Span { .begin = start, .end = start }));
    }
};

template<typename Rule>
constexpr auto not_at(RuleId id, Rule rule) -> NegativeLookaheadRule<Rule> {
    return NegativeLookaheadRule<Rule>(id, rstd::move(rule));
}

template<typename T, typename Rule, typename Adapter, typename Observer>
constexpr auto parse(Input<T> input, Rule& rule, Adapter& adapter, Observer& observer)
    -> Match<typename Rule::value_type, typename Adapter::error_type> {
    Driver<T, Adapter, Observer> driver(input, adapter, observer);
    return driver.apply(rule);
}

template<typename T, typename Rule, typename Adapter>
constexpr auto parse(Input<T> input, Rule& rule, Adapter& adapter)
    -> Match<typename Rule::value_type, typename Adapter::error_type> {
    NoopObserver                     observer;
    Driver<T, Adapter, NoopObserver> driver(input, adapter, observer);
    return driver.apply(rule);
}

template<typename T, typename Rule>
constexpr auto parse(Input<T> input, Rule& rule) -> Match<typename Rule::value_type> {
    BasicAdapter adapter;
    return parse(input, rule, adapter);
}

} // namespace rstd::parse
