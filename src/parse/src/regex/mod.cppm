export module rstd.parse.regex;
export import :syntax;
import :program;

using namespace rstd::prelude;

export namespace rstd::parse::regex
{

class Match {
    ref<str> input_;
    Span     span_ {};

public:
    constexpr Match(ref<str> input, Span span) noexcept: input_(input), span_(span) {}

    constexpr auto start() const noexcept -> usize { return span_.begin; }
    constexpr auto end() const noexcept -> usize { return span_.end; }
    constexpr auto span() const noexcept -> Span { return span_; }
    constexpr auto text() const noexcept [[clang::lifetimebound]] -> ref<str> {
        return input_.get(span_.begin, span_.end).unwrap();
    }
};

template<str_::fixed_string Pattern, Options OptionsValue>
class Regex;

template<str_::fixed_string Pattern, Options OptionsValue>
class Captures;

template<str_::fixed_string Pattern, Options OptionsValue>
constexpr auto consume_captures(TextCursor&                  cursor,
                                Regex<Pattern, OptionsValue> expression) noexcept
    -> Option<Captures<Pattern, OptionsValue>>;

template<str_::fixed_string Pattern, Options OptionsValue>
class Captures {
    static constexpr rstd::size_t CAPACITY = Syntax<Pattern.size()>::CAPTURE_CAPACITY;

    ref<str>     input_;
    rstd::size_t slots_[CAPACITY * 2] {};

    constexpr Captures(ref<str> input, const rstd::size_t* slots) noexcept: input_(input) {
        for (rstd::size_t index = 0; index < CAPACITY * 2; ++index) slots_[index] = slots[index];
    }

    constexpr auto rebased(ref<str> input, usize offset) const noexcept -> Captures {
        auto result   = *this;
        result.input_ = input;
        for (rstd::size_t index = 0; index < CAPACITY * 2; ++index) {
            if (result.slots_[index] != INVALID_INDEX) {
                result.slots_[index] += offset.to_primitive();
            }
        }
        return result;
    }

    friend class Regex<Pattern, OptionsValue>;

    template<str_::fixed_string OtherPattern, Options OtherOptions>
    friend constexpr auto consume_captures(TextCursor&                       cursor,
                                           Regex<OtherPattern, OtherOptions> expression) noexcept
        -> Option<Captures<OtherPattern, OtherOptions>>;

public:
    constexpr auto len() const noexcept -> usize {
        return usize(COMPILED_PATTERN<Pattern, OptionsValue>.syntax.capture_count);
    }

    constexpr auto get(usize index) const noexcept -> Option<Match> {
        if (index >= len()) return None();
        auto const start = slots_[index.to_primitive() * 2];
        auto const end   = slots_[index.to_primitive() * 2 + 1];
        if (start == INVALID_INDEX || end == INVALID_INDEX) return None();
        return Some(Match(input_, Span { usize(start), usize(end) }));
    }

    template<rstd::size_t Index>
    constexpr auto get() const noexcept -> Option<Match> {
        static_assert(Index < COMPILED_PATTERN<Pattern, OptionsValue>.syntax.capture_count,
                      "rstd regex capture index is out of bounds");
        return get(usize(Index));
    }

    template<str_::fixed_string Name>
    constexpr auto get() const noexcept -> Option<Match> {
        constexpr auto index = []() consteval {
            constexpr auto& compiled = COMPILED_PATTERN<Pattern, OptionsValue>;
            constexpr auto& bytes    = str_::BYTE_LITERAL_STORAGE<Pattern>;
            for (rstd::size_t capture = 1; capture < compiled.syntax.capture_count; ++capture) {
                auto const info = compiled.syntax.captures[capture];
                if (info.name_length != Name.size()) continue;
                auto same = true;
                for (rstd::size_t offset = 0; offset < Name.size(); ++offset) {
                    if (u8::from_byte(bytes[info.name_begin + offset]).to_primitive() !=
                        static_cast<rstd::uint8_t>(Name.data[offset])) {
                        same = false;
                        break;
                    }
                }
                if (same) return capture;
            }
            return INVALID_INDEX;
        }();
        static_assert(index != INVALID_INDEX, "rstd regex capture name is not present");
        return get(usize(index));
    }
};

template<str_::fixed_string Pattern, Options OptionsValue>
class Matches : public DefaultInClass<Matches<Pattern, OptionsValue>, iter::Iterator> {
    ref<str> input_;
    usize    position_ {};
    bool     finished_ {};

public:
    using Item                         = Match;
    static constexpr bool PROVEN_FUSED = true;

    constexpr explicit Matches(ref<str> input) noexcept: input_(input) {}

    constexpr auto next() noexcept -> Option<Match>;
};

template<str_::fixed_string Pattern, Options OptionsValue>
class Regex {
    static constexpr auto make_match(ref<str>                                input,
                                     const RawResult<Pattern.size(), false>& result) noexcept
        -> Option<Match> {
        if (! result.matched) return None();
        return Some(Match(input, Span { usize(result.begin), usize(result.end) }));
    }

public:
    static constexpr auto capture_count() noexcept -> usize {
        return usize(COMPILED_PATTERN<Pattern, OptionsValue>.syntax.capture_count);
    }

    static constexpr auto nullable() noexcept -> bool {
        return COMPILED_PATTERN<Pattern, OptionsValue>.nullable;
    }

    constexpr auto is_match(ref<str> input) const noexcept -> bool {
        return run<Pattern, OptionsValue, false>(input, 0, RunMode::Search).matched;
    }

    constexpr auto find(ref<str> input) const noexcept -> Option<Match> {
        return find_from(input, usize());
    }

    constexpr auto find_from(ref<str> input, usize position) const noexcept -> Option<Match> {
        if (position > input.len() || ! input.is_char_boundary(position)) return None();
        return make_match(
            input,
            run<Pattern, OptionsValue, false>(input, position.to_primitive(), RunMode::Search));
    }

    constexpr auto prefix(ref<str> input) const noexcept -> Option<Match> {
        return make_match(input, run<Pattern, OptionsValue, false>(input, 0, RunMode::Prefix));
    }

    constexpr auto starts_with(ref<str> input) const noexcept -> bool {
        return prefix(input).is_some();
    }

    constexpr auto full_match(ref<str> input) const noexcept -> Option<Match> {
        return make_match(input, run<Pattern, OptionsValue, false>(input, 0, RunMode::Full));
    }

    constexpr auto captures(ref<str> input) const noexcept
        -> Option<Captures<Pattern, OptionsValue>> {
        auto result = run<Pattern, OptionsValue, true>(input, 0, RunMode::Search);
        if (! result.matched) return None();
        return Some(Captures<Pattern, OptionsValue>(input, result.slots));
    }

    constexpr auto prefix_captures(ref<str> input) const noexcept
        -> Option<Captures<Pattern, OptionsValue>> {
        auto result = run<Pattern, OptionsValue, true>(input, 0, RunMode::Prefix);
        if (! result.matched) return None();
        return Some(Captures<Pattern, OptionsValue>(input, result.slots));
    }

    constexpr auto find_iter(ref<str> input) const noexcept -> Matches<Pattern, OptionsValue> {
        return Matches<Pattern, OptionsValue>(input);
    }
};

template<str_::fixed_string Pattern, Options OptionsValue>
constexpr auto Matches<Pattern, OptionsValue>::next() noexcept -> Option<Match> {
    if (finished_) return None();
    auto found = Regex<Pattern, OptionsValue> {}.find_from(input_, position_);
    if (found.is_none()) {
        finished_ = true;
        return None();
    }
    auto const end = found->end();
    if (found->start() != end) {
        position_ = end;
    } else if (end == input_.len()) {
        finished_ = true;
    } else {
        auto remaining = input_.get(end, input_.len()).unwrap_unchecked();
        auto indices   = remaining.char_indices();
        (void)indices.next();
        position_ = input_.len() - indices.as_str().len();
    }
    return found;
}

template<str_::fixed_string Pattern, Options OptionsValue = {}>
inline constexpr auto compile = []() consteval {
    constexpr auto error = COMPILED_PATTERN<Pattern, OptionsValue>.error;
    require_valid_pattern<error.kind, error.byte_offset>();
    return Regex<Pattern, OptionsValue> {};
}();

template<str_::fixed_string Pattern, Options OptionsValue>
constexpr auto consume(TextCursor& cursor, Regex<Pattern, OptionsValue> expression) noexcept
    -> Option<Span> {
    auto match = expression.prefix(cursor.remaining_text());
    if (match.is_none()) return None();
    auto const begin = cursor.position();
    cursor.advance(match->span().len());
    return Some(Span { begin, cursor.position() });
}

template<str_::fixed_string Pattern, Options OptionsValue>
constexpr auto consume_captures(TextCursor&                  cursor,
                                Regex<Pattern, OptionsValue> expression) noexcept
    -> Option<Captures<Pattern, OptionsValue>> {
    auto captures = expression.prefix_captures(cursor.remaining_text());
    if (captures.is_none()) return None();
    auto const begin  = cursor.position();
    auto const length = captures->template get<0>()->span().len();
    cursor.advance(length);
    return Some(captures->rebased(cursor.text(Span { usize(), cursor.len() }), begin));
}

} // namespace rstd::parse::regex
