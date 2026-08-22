export module rstd.parse:rule;
export import :error;

using namespace rstd::prelude;
using ::alloc::vec::Vec;

export namespace rstd::parse
{

struct NoopObserver {
    constexpr void start(RuleId, usize) noexcept {}
    constexpr void success(RuleId, Span) noexcept {}
    constexpr void mismatch(RuleId, usize) noexcept {}
    constexpr void error(RuleId, const ParseError&) noexcept {}
};

template<typename T, typename Observer>
class Driver {
    SourceId  source_;
    Cursor<T> cursor_;
    Observer& observer_;

public:
    Driver(Input<T> input, SourceId source, Observer& observer)
        : source_(rstd::move(source)), cursor_(input), observer_(observer) {}

    auto cursor() noexcept [[clang::lifetimebound]] -> Cursor<T>& { return cursor_; }
    auto cursor() const noexcept [[clang::lifetimebound]] -> const Cursor<T>& { return cursor_; }
    auto source() const noexcept [[clang::lifetimebound]] -> const SourceId& { return source_; }

    template<typename Rule>
    auto apply(Rule& rule) -> Match<typename Rule::value_type> {
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

    auto expected(RuleId rule) const -> ParseError {
        auto offset = cursor_.furthest_position();
        return ParseError::expected(source_.clone(), Span { .begin = offset, .end = offset }, rule);
    }

    auto stalled(RuleId rule, usize offset) const -> ParseError {
        return ParseError::stalled(source_.clone(), Span { .begin = offset, .end = offset }, rule);
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

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<Span> {
        auto start = driver.cursor().checkpoint();
        auto value = driver.cursor().peek();
        if (value.is_none() || ! predicate_(value->get())) return Ok<Option<Span>>(None());
        (void)driver.cursor().take();
        return Ok(Some(driver.cursor().span_from(start)));
    }
};

template<typename Predicate>
auto atomic(RuleId id, Predicate predicate) -> AtomicRule<Predicate> {
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

    template<typename Observer>
    auto match(Driver<u8, Observer>& driver) -> Match<Span> {
        auto start = driver.cursor().checkpoint();
        for (auto expected : text_.bytes()) {
            auto actual = driver.cursor().take();
            if (actual.is_none() || actual->get() != expected) return Ok<Option<Span>>(None());
        }
        return Ok(Some(driver.cursor().span_from(start)));
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

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<Span> {
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

    SequenceRule(RuleId id, Left left, Right right)
        : id_(id), left_(rstd::move(left)), right_(rstd::move(right)) {}

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<value_type> {
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
auto seq(RuleId id, Left left, Right right) -> SequenceRule<Left, Right> {
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

    DelimitedRule(RuleId id, Open open, Rule rule, Close close)
        : id_(id), open_(rstd::move(open)), rule_(rstd::move(rule)), close_(rstd::move(close)) {}

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<value_type> {
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
auto delimited(RuleId id, Open open, Rule rule, Close close) -> DelimitedRule<Open, Rule, Close> {
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

    ChoiceRule(RuleId id, Left left, Right right)
        : id_(id), left_(rstd::move(left)), right_(rstd::move(right)) {}

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<value_type> {
        auto left = driver.apply(left_);
        if (left.is_err() || left->is_some()) return left;
        return driver.apply(right_);
    }
};

template<typename Left, typename Right>
auto choice(RuleId id, Left left, Right right) -> ChoiceRule<Left, Right> {
    return ChoiceRule<Left, Right>(id, rstd::move(left), rstd::move(right));
}

template<typename Rule>
class OptionalRule {
    RuleId id_;
    Rule   rule_;

public:
    using value_type               = Option<typename Rule::value_type>;
    static constexpr bool nullable = true;

    OptionalRule(RuleId id, Rule rule): id_(id), rule_(rstd::move(rule)) {}
    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<value_type> {
        auto value = driver.apply(rule_);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        auto optional = rstd::move(value).unwrap_unchecked();
        if (optional.is_none()) return Ok(Some(value_type(None())));
        return Ok(Some(value_type(Some(rstd::move(optional).unwrap_unchecked()))));
    }
};

template<typename Rule>
auto optional(RuleId id, Rule rule) -> OptionalRule<Rule> {
    return OptionalRule<Rule>(id, rstd::move(rule));
}

template<typename Rule>
class RepeatRule {
    RuleId id_;
    Rule   rule_;
    bool   require_one_;

public:
    using value_type               = Vec<typename Rule::value_type>;
    static constexpr bool nullable = true;

    RepeatRule(RuleId id, Rule rule, bool require_one)
        : id_(id), rule_(rstd::move(rule)), require_one_(require_one) {
        static_assert(! Rule::nullable, "repeat requires a non-nullable rule");
    }

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<value_type> {
        auto values = value_type::make();
        for (;;) {
            auto before = driver.cursor().position();
            auto value  = driver.apply(rule_);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
            auto optional = rstd::move(value).unwrap_unchecked();
            if (optional.is_none()) break;
            if (driver.cursor().position() == before)
                return Err(driver.stalled(rule_.id(), before));
            values.push(rstd::move(optional).unwrap_unchecked());
        }
        if (require_one_ && values.is_empty()) return Ok<Option<value_type>>(None());
        return Ok(Some(rstd::move(values)));
    }
};

template<typename Rule>
auto repeat(RuleId id, Rule rule) -> RepeatRule<Rule> {
    return RepeatRule<Rule>(id, rstd::move(rule), false);
}

template<typename Rule>
auto repeat_one(RuleId id, Rule rule) -> RepeatRule<Rule> {
    return RepeatRule<Rule>(id, rstd::move(rule), true);
}

template<typename Rule, typename Separator>
class SeparatedRule {
    RuleId    id_;
    Rule      rule_;
    Separator separator_;
    bool      require_one_;

public:
    using value_type               = Vec<typename Rule::value_type>;
    static constexpr bool nullable = true;

    SeparatedRule(RuleId id, Rule rule, Separator separator, bool require_one)
        : id_(id),
          rule_(rstd::move(rule)),
          separator_(rstd::move(separator)),
          require_one_(require_one) {
        static_assert(! Rule::nullable, "separated requires a non-nullable value rule");
        static_assert(! Separator::nullable, "separated requires a non-nullable separator");
    }

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<value_type> {
        auto values = value_type::make();
        auto first  = driver.apply(rule_);
        if (first.is_err()) return Err(rstd::move(first).unwrap_err_unchecked());
        auto first_value = rstd::move(first).unwrap_unchecked();
        if (first_value.is_none()) {
            if (require_one_) return Ok<Option<value_type>>(None());
            return Ok(Some(rstd::move(values)));
        }
        values.push(rstd::move(first_value).unwrap_unchecked());

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
            values.push(rstd::move(optional).unwrap_unchecked());
        }
        return Ok(Some(rstd::move(values)));
    }
};

template<typename Rule, typename Separator>
auto separated(RuleId id, Rule rule, Separator separator) -> SeparatedRule<Rule, Separator> {
    return SeparatedRule<Rule, Separator>(id, rstd::move(rule), rstd::move(separator), false);
}

template<typename Rule, typename Separator>
auto separated_one(RuleId id, Rule rule, Separator separator) -> SeparatedRule<Rule, Separator> {
    return SeparatedRule<Rule, Separator>(id, rstd::move(rule), rstd::move(separator), true);
}

template<typename Rule, typename Function>
class MapRule {
    RuleId   id_;
    Rule     rule_;
    Function function_;

public:
    using value_type               = mtp::invoke_result_t<Function, typename Rule::value_type>;
    static constexpr bool nullable = Rule::nullable;

    MapRule(RuleId id, Rule rule, Function function)
        : id_(id), rule_(rstd::move(rule)), function_(rstd::move(function)) {}

    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<value_type> {
        auto value = driver.apply(rule_);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        auto optional = rstd::move(value).unwrap_unchecked();
        if (optional.is_none()) return Ok<Option<value_type>>(None());
        return Ok(Some(function_(rstd::move(optional).unwrap_unchecked())));
    }
};

template<typename Rule, typename Function>
auto map(RuleId id, Rule rule, Function function) -> MapRule<Rule, Function> {
    return MapRule<Rule, Function>(id, rstd::move(rule), rstd::move(function));
}

template<typename Rule>
class CommitRule {
    RuleId id_;
    Rule   rule_;

public:
    using value_type               = typename Rule::value_type;
    static constexpr bool nullable = Rule::nullable;

    CommitRule(RuleId id, Rule rule): id_(id), rule_(rstd::move(rule)) {}
    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<value_type> {
        auto value = driver.apply(rule_);
        if (value.is_err()) return value;
        if (value->is_none()) return Err(driver.expected(rule_.id()));
        return value;
    }
};

template<typename Rule>
auto commit(RuleId id, Rule rule) -> CommitRule<Rule> {
    return CommitRule<Rule>(id, rstd::move(rule));
}

template<typename Rule>
class LookaheadRule {
    RuleId id_;
    Rule   rule_;

public:
    using value_type               = typename Rule::value_type;
    static constexpr bool nullable = true;

    LookaheadRule(RuleId id, Rule rule): id_(id), rule_(rstd::move(rule)) {}
    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<value_type> {
        auto checkpoint = driver.cursor().checkpoint();
        auto value      = driver.apply(rule_);
        driver.cursor().rewind(checkpoint);
        return value;
    }
};

template<typename Rule>
auto lookahead(RuleId id, Rule rule) -> LookaheadRule<Rule> {
    return LookaheadRule<Rule>(id, rstd::move(rule));
}

template<typename Rule>
class NegativeLookaheadRule {
    RuleId id_;
    Rule   rule_;

public:
    using value_type               = Span;
    static constexpr bool nullable = true;

    NegativeLookaheadRule(RuleId id, Rule rule): id_(id), rule_(rstd::move(rule)) {}
    constexpr auto id() const noexcept -> RuleId { return id_; }

    template<typename T, typename Observer>
    auto match(Driver<T, Observer>& driver) -> Match<Span> {
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
auto not_at(RuleId id, Rule rule) -> NegativeLookaheadRule<Rule> {
    return NegativeLookaheadRule<Rule>(id, rstd::move(rule));
}

template<typename T, typename Observer, typename Rule>
auto parse(Input<T> input, SourceId source, Rule& rule, Observer& observer)
    -> Match<typename Rule::value_type> {
    Driver<T, Observer> driver(input, rstd::move(source), observer);
    return driver.apply(rule);
}

template<typename T, typename Rule>
auto parse(Input<T> input, SourceId source, Rule& rule) -> Match<typename Rule::value_type> {
    NoopObserver            observer;
    Driver<T, NoopObserver> driver(input, rstd::move(source), observer);
    return driver.apply(rule);
}

} // namespace rstd::parse
