export module rstd.parse.core:predicate;
export import :source;

using namespace rstd::prelude;

export namespace rstd::parse
{

template<typename Predicate, typename Value>
concept PredicateFor = requires(const Predicate& predicate, const Value& value) {
    { predicate(value) } -> mtp::convertible_to<bool>;
};

template<typename Function>
class FunctionPredicate {
    Function function_;

public:
    constexpr explicit FunctionPredicate(Function function): function_(rstd::move(function)) {}

    template<typename Value>
        requires PredicateFor<Function, Value>
    constexpr auto operator()(const Value& value) const
        noexcept(noexcept(static_cast<bool>(function_(value)))) -> bool {
        return static_cast<bool>(function_(value));
    }
};

template<typename Function>
constexpr auto predicate(Function function) -> FunctionPredicate<Function> {
    return FunctionPredicate<Function>(rstd::move(function));
}

template<typename Value>
class EqualPredicate {
    Value expected_;

public:
    constexpr explicit EqualPredicate(Value expected): expected_(rstd::move(expected)) {}

    template<typename Actual>
    constexpr auto operator()(const Actual& actual) const
        noexcept(noexcept(static_cast<bool>(actual == expected_))) -> bool {
        return static_cast<bool>(actual == expected_);
    }
};

template<typename Value>
constexpr auto equal_to(Value value) -> EqualPredicate<Value> {
    return EqualPredicate<Value>(rstd::move(value));
}

template<typename Value>
class RangePredicate {
    Value low_;
    Value high_;

public:
    constexpr RangePredicate(Value low, Value high)
        : low_(rstd::move(low)), high_(rstd::move(high)) {}

    template<typename Actual>
    constexpr auto operator()(const Actual& actual) const
        noexcept(noexcept(static_cast<bool>(actual >= low_ && actual <= high_))) -> bool {
        return static_cast<bool>(actual >= low_ && actual <= high_);
    }
};

template<typename Value>
constexpr auto range(Value low, Value high) -> RangePredicate<Value> {
    return RangePredicate<Value>(rstd::move(low), rstd::move(high));
}

template<typename... Values>
class OneOfPredicate {
    tuple<Values...> values_;

    template<rstd::size_t... Indices, typename Actual>
    constexpr auto matches(mtp::index_sequence<Indices...>, const Actual& actual) const -> bool {
        return ((actual == rstd::get<Indices>(values_)) || ...);
    }

public:
    constexpr explicit OneOfPredicate(Values... values): values_(rstd::move(values)...) {}

    template<typename Actual>
    constexpr auto operator()(const Actual& actual) const -> bool {
        return matches(mtp::make_index_sequence<sizeof...(Values)> {}, actual);
    }
};

template<typename... Values>
constexpr auto one_of(Values... values) -> OneOfPredicate<Values...> {
    static_assert(sizeof...(Values) > 0, "one_of requires at least one value");
    return OneOfPredicate<Values...>(rstd::move(values)...);
}

template<typename Predicate>
class NotPredicate {
    Predicate predicate_;

public:
    constexpr explicit NotPredicate(Predicate predicate): predicate_(rstd::move(predicate)) {}

    template<typename Value>
        requires PredicateFor<Predicate, Value>
    constexpr auto operator()(const Value& value) const -> bool {
        return ! predicate_(value);
    }
};

template<typename Predicate>
constexpr auto not_(Predicate predicate) -> NotPredicate<Predicate> {
    return NotPredicate<Predicate>(rstd::move(predicate));
}

template<bool RequireAll, typename... Predicates>
class CombinedPredicate {
    tuple<Predicates...> predicates_;

    template<rstd::size_t... Indices, typename Value>
    constexpr auto matches(mtp::index_sequence<Indices...>, const Value& value) const -> bool {
        if constexpr (RequireAll) {
            return (rstd::get<Indices>(predicates_)(value) && ...);
        } else {
            return (rstd::get<Indices>(predicates_)(value) || ...);
        }
    }

public:
    constexpr explicit CombinedPredicate(Predicates... predicates)
        : predicates_(rstd::move(predicates)...) {}

    template<typename Value>
        requires(PredicateFor<Predicates, Value> && ...)
    constexpr auto operator()(const Value& value) const -> bool {
        return matches(mtp::make_index_sequence<sizeof...(Predicates)> {}, value);
    }
};

template<typename... Predicates>
constexpr auto any_of(Predicates... predicates) -> CombinedPredicate<false, Predicates...> {
    static_assert(sizeof...(Predicates) > 0, "any_of requires at least one predicate");
    return CombinedPredicate<false, Predicates...>(rstd::move(predicates)...);
}

template<typename... Predicates>
constexpr auto all_of(Predicates... predicates) -> CombinedPredicate<true, Predicates...> {
    static_assert(sizeof...(Predicates) > 0, "all_of requires at least one predicate");
    return CombinedPredicate<true, Predicates...>(rstd::move(predicates)...);
}

namespace ascii
{

inline constexpr auto digit     = predicate(&rstd::ascii::is_digit);
inline constexpr auto hex_digit = predicate(&rstd::ascii::is_hex_digit);
inline constexpr auto alpha     = predicate(&rstd::ascii::is_alpha);
inline constexpr auto alnum     = predicate(&rstd::ascii::is_alnum);
inline constexpr auto lower     = predicate(&rstd::ascii::is_lower);
inline constexpr auto upper     = predicate(&rstd::ascii::is_upper);
inline constexpr auto blank     = predicate(&rstd::ascii::is_blank);
inline constexpr auto space     = predicate(&rstd::ascii::is_space);

} // namespace ascii

} // namespace rstd::parse
