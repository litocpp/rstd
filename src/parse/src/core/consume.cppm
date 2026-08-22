export module rstd.parse.core:consume;
export import :predicate;

using namespace rstd::prelude;

export namespace rstd::parse
{

template<typename T, typename Predicate>
    requires PredicateFor<Predicate, T>
constexpr auto consume_if(Cursor<T>& cursor, const Predicate& predicate) noexcept -> Option<Span> {
    auto start = cursor.checkpoint();
    auto value = cursor.peek();
    if (value.is_none() || ! predicate(value->get())) return None();
    (void)cursor.take();
    return Some(cursor.span_from(start));
}

template<typename T, typename Predicate>
    requires PredicateFor<Predicate, T>
constexpr auto consume_while(Cursor<T>& cursor, const Predicate& predicate) noexcept -> Span {
    auto start = cursor.checkpoint();
    while (true) {
        auto value = cursor.peek();
        if (value.is_none() || ! predicate(value->get())) break;
        (void)cursor.take();
    }
    return cursor.span_from(start);
}

template<typename T, typename Predicate>
    requires PredicateFor<Predicate, T>
constexpr auto consume_while_one(Cursor<T>& cursor, const Predicate& predicate) noexcept
    -> Option<Span> {
    auto span = consume_while(cursor, predicate);
    if (span.is_empty()) return None();
    return Some(span);
}

template<typename T, typename Predicate>
    requires PredicateFor<Predicate, T>
constexpr auto consume_n(Cursor<T>& cursor, usize count, const Predicate& predicate) noexcept
    -> Option<Span> {
    auto start = cursor.checkpoint();
    for (usize index {}; index < count; ++index) {
        auto value = cursor.take();
        if (value.is_none() || ! predicate(value->get())) {
            cursor.rewind(start);
            return None();
        }
    }
    return Some(cursor.span_from(start));
}

constexpr auto consume_literal(TextCursor& cursor, ref<str> text) noexcept -> Option<Span> {
    auto start = cursor.checkpoint();
    for (auto expected : text.bytes()) {
        auto actual = cursor.take();
        if (actual.is_none() || actual->get() != expected) {
            cursor.rewind(start);
            return None();
        }
    }
    return Some(cursor.span_from(start));
}

template<typename T, typename Predicate>
    requires PredicateFor<Predicate, T>
constexpr auto consume_until(Cursor<T>& cursor, const Predicate& predicate) noexcept -> Span {
    return consume_while(cursor, not_(predicate));
}

template<typename T, typename Predicate>
    requires PredicateFor<Predicate, T>
constexpr auto skip_while(Cursor<T>& cursor, const Predicate& predicate) noexcept -> usize {
    return consume_while(cursor, predicate).len();
}

} // namespace rstd::parse
