export module rstd.parse.regex:syntax;
export import rstd.parse.core;

using namespace rstd::prelude;

export namespace rstd::parse::regex
{

struct Options {
    bool multiline {};
    bool dot_matches_newline {};
    bool case_insensitive {};

    constexpr auto operator==(const Options&) const noexcept -> bool = default;
};

enum class RegexErrorKind : rstd::uint8_t
{
    None,
    InvalidUtf8,
    UnexpectedEnd,
    UnexpectedToken,
    UnexpectedQuantifier,
    UnclosedGroup,
    UnclosedClass,
    EmptyClass,
    InvalidEscape,
    InvalidRange,
    InvalidRepeat,
    InvalidCaptureName,
    DuplicateCaptureName,
    UnsupportedConstruct,
    ProgramTooLarge,
};

struct RegexSyntaxError {
    RegexErrorKind kind { RegexErrorKind::None };
    rstd::size_t   byte_offset {};

    constexpr explicit operator bool() const noexcept { return kind != RegexErrorKind::None; }
};

} // namespace rstd::parse::regex

namespace rstd::parse::regex
{

inline constexpr rstd::size_t INVALID_INDEX = static_cast<rstd::size_t>(-1);
inline constexpr rstd::size_t REPEAT_LIMIT  = 256;

enum class NodeKind : rstd::uint8_t
{
    Empty,
    Literal,
    Any,
    CharacterClass,
    Concat,
    Alternate,
    Repeat,
    Group,
    AssertSubjectStart,
    AssertSubjectEnd,
    AssertLineStart,
    AssertLineEnd,
    AssertWordBoundary,
    AssertNotWordBoundary,
};

enum class ClassTermKind : rstd::uint8_t
{
    Range,
    Digit,
    Space,
    Word
};

struct ClassTerm {
    ClassTermKind kind { ClassTermKind::Range };
    char32_t      low {};
    char32_t      high {};
    bool          negated {};
};

struct Node {
    NodeKind     kind { NodeKind::Empty };
    rstd::size_t left { INVALID_INDEX };
    rstd::size_t right { INVALID_INDEX };
    char32_t     value {};
    rstd::size_t class_begin {};
    rstd::size_t class_count {};
    rstd::size_t minimum {};
    rstd::size_t maximum {};
    rstd::size_t capture {};
    bool         negated {};
    bool         greedy { true };
};

struct CaptureInfo {
    rstd::size_t name_begin {};
    rstd::size_t name_length {};
};

template<rstd::size_t PatternSize>
struct Syntax {
    static constexpr rstd::size_t NODE_CAPACITY    = PatternSize * 4 + 16;
    static constexpr rstd::size_t TERM_CAPACITY    = PatternSize * 2 + 16;
    static constexpr rstd::size_t CAPTURE_CAPACITY = PatternSize / 2 + 2;

    Node             nodes[NODE_CAPACITY] {};
    ClassTerm        terms[TERM_CAPACITY] {};
    CaptureInfo      captures[CAPTURE_CAPACITY] {};
    rstd::size_t     node_count {};
    rstd::size_t     term_count {};
    rstd::size_t     capture_count { 1 };
    rstd::size_t     root { INVALID_INDEX };
    RegexSyntaxError error {};
};

enum class EscapeKind : rstd::uint8_t
{
    Literal,
    Class,
    Assertion
};

struct EscapeResult {
    EscapeKind kind { EscapeKind::Literal };
    char32_t   literal {};
    ClassTerm  term {};
    NodeKind   assertion { NodeKind::Empty };
};

template<str_::fixed_string Pattern, Options OptionsValue>
class PatternParser {
    static constexpr auto& BYTES = str_::BYTE_LITERAL_STORAGE<Pattern>;
    using SyntaxType             = Syntax<Pattern.size()>;

    TextCursor cursor_;
    SyntaxType syntax_ {};

    static consteval auto pattern_text() noexcept -> ref<str> {
        return ref<str>::from_raw_parts_unchecked(BYTES.data(), usize(Pattern.size()));
    }

    consteval auto position() const noexcept -> rstd::size_t {
        return cursor_.position().to_primitive();
    }

    consteval auto at_end() const noexcept -> bool { return cursor_.is_eof(); }

    consteval auto byte_at(rstd::size_t index) const noexcept -> rstd::uint8_t {
        return u8::from_byte(BYTES[index]).to_primitive();
    }

    consteval auto peek_ascii(char expected) const noexcept -> bool {
        auto value = cursor_.peek();
        return value.is_some() && value->get() == u8(static_cast<rstd::uint8_t>(expected));
    }

    consteval auto consume_ascii(char expected) noexcept -> bool {
        return consume_if(cursor_, equal_to(u8(static_cast<rstd::uint8_t>(expected)))).is_some();
    }

    consteval void fail(RegexErrorKind kind, rstd::size_t offset) noexcept {
        if (! syntax_.error) syntax_.error = RegexSyntaxError { kind, offset };
    }

    consteval auto add_node(Node node) noexcept -> rstd::size_t {
        if (syntax_.node_count >= SyntaxType::NODE_CAPACITY) {
            fail(RegexErrorKind::ProgramTooLarge, position());
            return INVALID_INDEX;
        }
        auto const index     = syntax_.node_count++;
        syntax_.nodes[index] = node;
        return index;
    }

    consteval auto add_term(ClassTerm term) noexcept -> bool {
        if (syntax_.term_count >= SyntaxType::TERM_CAPACITY) {
            fail(RegexErrorKind::ProgramTooLarge, position());
            return false;
        }
        syntax_.terms[syntax_.term_count++] = term;
        return true;
    }

    consteval auto read_codepoint() noexcept -> char32_t {
        if (at_end()) {
            fail(RegexErrorKind::UnexpectedEnd, position());
            return char32_t();
        }
        auto const remaining  = cursor_.remaining_text();
        auto       indices    = remaining.char_indices();
        auto const code_point = get<1>(indices.next().unwrap_unchecked());
        auto const length     = remaining.len() - indices.as_str().len();
        (void)cursor_.advance(length);
        return static_cast<char32_t>(code_point.to_primitive());
    }

    consteval auto read_hex_digit() noexcept -> Option<rstd::uint8_t> {
        if (at_end()) return None();
        auto const value = rstd::ascii::digit_value(cursor_.peek()->get(), u8(16));
        if (value.is_none()) return None();
        (void)cursor_.take();
        return Some(value->to_primitive());
    }

    consteval auto read_hex_escape() noexcept -> char32_t {
        auto const     offset = position();
        rstd::uint32_t value {};
        for (rstd::size_t count = 0; count < 2; ++count) {
            auto digit = read_hex_digit();
            if (digit.is_none()) {
                fail(RegexErrorKind::InvalidEscape, offset);
                return char32_t();
            }
            value = value * 16 + *digit;
        }
        return static_cast<char32_t>(value);
    }

    consteval auto read_unicode_escape() noexcept -> char32_t {
        auto const offset = position();
        if (! consume_ascii('{')) {
            fail(RegexErrorKind::InvalidEscape, offset);
            return char32_t();
        }
        rstd::uint32_t value {};
        rstd::size_t   digits {};
        while (! at_end() && ! peek_ascii('}')) {
            auto digit = read_hex_digit();
            if (digit.is_none() || digits == 6) {
                fail(RegexErrorKind::InvalidEscape, offset);
                return char32_t();
            }
            value = value * 16 + *digit;
            ++digits;
        }
        if (digits == 0 || ! consume_ascii('}') || value > char_::MAX ||
            (value >= 0xd800 && value <= 0xdfff)) {
            fail(RegexErrorKind::InvalidEscape, offset);
            return char32_t();
        }
        return static_cast<char32_t>(value);
    }

    consteval auto parse_escape(bool in_class) noexcept -> EscapeResult {
        auto const offset = position();
        if (at_end()) {
            fail(RegexErrorKind::InvalidEscape, offset);
            return {};
        }
        auto const value = static_cast<char>(cursor_.take()->get().to_primitive());
        switch (value) {
        case 'd':
            return { .kind = EscapeKind::Class,
                     .term = ClassTerm { .kind = ClassTermKind::Digit } };
        case 'D':
            return { .kind = EscapeKind::Class,
                     .term = ClassTerm { .kind = ClassTermKind::Digit, .negated = true } };
        case 's':
            return { .kind = EscapeKind::Class,
                     .term = ClassTerm { .kind = ClassTermKind::Space } };
        case 'S':
            return { .kind = EscapeKind::Class,
                     .term = ClassTerm { .kind = ClassTermKind::Space, .negated = true } };
        case 'w':
            return { .kind = EscapeKind::Class, .term = ClassTerm { .kind = ClassTermKind::Word } };
        case 'W':
            return { .kind = EscapeKind::Class,
                     .term = ClassTerm { .kind = ClassTermKind::Word, .negated = true } };
        case 'b':
            if (in_class) return { .literal = char32_t(8) };
            return { .kind = EscapeKind::Assertion, .assertion = NodeKind::AssertWordBoundary };
        case 'B':
            if (in_class) break;
            return { .kind = EscapeKind::Assertion, .assertion = NodeKind::AssertNotWordBoundary };
        case 'A':
            if (in_class) break;
            return { .kind = EscapeKind::Assertion, .assertion = NodeKind::AssertSubjectStart };
        case 'z':
            if (in_class) break;
            return { .kind = EscapeKind::Assertion, .assertion = NodeKind::AssertSubjectEnd };
        case 'n': return { .literal = U'\n' };
        case 'r': return { .literal = U'\r' };
        case 't': return { .literal = U'\t' };
        case '0': return { .literal = char32_t() };
        case 'x': return { .literal = read_hex_escape() };
        case 'u': return { .literal = read_unicode_escape() };
        case '.':
        case '[':
        case ']':
        case '{':
        case '}':
        case '(':
        case ')':
        case '|':
        case '*':
        case '+':
        case '?':
        case '^':
        case '$':
        case '-':
        case '\\': return { .literal = static_cast<char32_t>(value) };
        default: break;
        }
        fail(rstd::ascii::is_digit(u8(static_cast<rstd::uint8_t>(value)))
                 ? RegexErrorKind::UnsupportedConstruct
                 : RegexErrorKind::InvalidEscape,
             offset);
        return {};
    }

    consteval auto capture_name_equal(rstd::size_t capture,
                                      rstd::size_t begin,
                                      rstd::size_t length) const noexcept -> bool {
        auto const info = syntax_.captures[capture];
        if (info.name_length != length) return false;
        for (rstd::size_t index = 0; index < length; ++index) {
            if (byte_at(info.name_begin + index) != byte_at(begin + index)) return false;
        }
        return true;
    }

    consteval auto create_capture(rstd::size_t name_begin, rstd::size_t name_length) noexcept
        -> rstd::size_t {
        if (name_length != 0) {
            for (rstd::size_t index = 1; index < syntax_.capture_count; ++index) {
                if (capture_name_equal(index, name_begin, name_length)) {
                    fail(RegexErrorKind::DuplicateCaptureName, name_begin);
                    return 0;
                }
            }
        }
        if (syntax_.capture_count >= SyntaxType::CAPTURE_CAPACITY) {
            fail(RegexErrorKind::ProgramTooLarge, name_begin);
            return 0;
        }
        auto const capture        = syntax_.capture_count++;
        syntax_.captures[capture] = CaptureInfo { name_begin, name_length };
        return capture;
    }

    consteval auto parse_class_atom() noexcept -> ClassTerm {
        if (consume_ascii('\\')) {
            auto escaped = parse_escape(true);
            if (escaped.kind == EscapeKind::Class) return escaped.term;
            if (escaped.kind == EscapeKind::Assertion) {
                fail(RegexErrorKind::InvalidEscape, position());
                return {};
            }
            return ClassTerm { .kind = ClassTermKind::Range,
                               .low  = escaped.literal,
                               .high = escaped.literal };
        }
        auto const value = read_codepoint();
        return ClassTerm { .kind = ClassTermKind::Range, .low = value, .high = value };
    }

    consteval auto parse_class() noexcept -> rstd::size_t {
        auto const class_offset = position() - 1;
        auto const begin        = syntax_.term_count;
        auto const negated      = consume_ascii('^');
        while (! syntax_.error && ! at_end() && ! peek_ascii(']')) {
            auto term = parse_class_atom();
            if (syntax_.error) return INVALID_INDEX;
            if (term.kind == ClassTermKind::Range && peek_ascii('-') &&
                position() + 1 < Pattern.size() && byte_at(position() + 1) != ']') {
                (void)cursor_.take();
                auto high = parse_class_atom();
                if (high.kind != ClassTermKind::Range || high.low < term.low) {
                    fail(RegexErrorKind::InvalidRange, position());
                    return INVALID_INDEX;
                }
                term.high = high.low;
            }
            if (! add_term(term)) return INVALID_INDEX;
        }
        if (! consume_ascii(']')) {
            fail(RegexErrorKind::UnclosedClass, class_offset);
            return INVALID_INDEX;
        }
        if (syntax_.term_count == begin) {
            fail(RegexErrorKind::EmptyClass, class_offset);
            return INVALID_INDEX;
        }
        return add_node(Node { .kind        = NodeKind::CharacterClass,
                               .class_begin = begin,
                               .class_count = syntax_.term_count - begin,
                               .negated     = negated });
    }

    consteval auto parse_group() noexcept -> rstd::size_t {
        auto const   group_offset = position() - 1;
        bool         capturing    = true;
        rstd::size_t name_begin {};
        rstd::size_t name_length {};

        if (consume_ascii('?')) {
            if (consume_ascii(':')) {
                capturing = false;
            } else if (consume_ascii('<')) {
                name_begin = position();
                if (at_end() ||
                    ! (rstd::ascii::is_alpha(cursor_.peek()->get()) || peek_ascii('_'))) {
                    fail(RegexErrorKind::InvalidCaptureName, position());
                    return INVALID_INDEX;
                }
                (void)cursor_.take();
                while (! at_end() &&
                       (rstd::ascii::is_alnum(cursor_.peek()->get()) || peek_ascii('_'))) {
                    (void)cursor_.take();
                }
                name_length = position() - name_begin;
                if (! consume_ascii('>')) {
                    fail(RegexErrorKind::InvalidCaptureName, name_begin);
                    return INVALID_INDEX;
                }
            } else {
                fail(RegexErrorKind::UnsupportedConstruct, position() - 1);
                return INVALID_INDEX;
            }
        }

        auto const capture = capturing ? create_capture(name_begin, name_length) : 0;
        auto const child   = parse_alternation();
        if (! consume_ascii(')')) {
            fail(RegexErrorKind::UnclosedGroup, group_offset);
            return INVALID_INDEX;
        }
        if (! capturing) return child;
        return add_node(Node { .kind = NodeKind::Group, .left = child, .capture = capture });
    }

    consteval auto parse_atom() noexcept -> rstd::size_t {
        if (at_end()) {
            fail(RegexErrorKind::UnexpectedEnd, position());
            return INVALID_INDEX;
        }
        if (consume_ascii('(')) return parse_group();
        if (consume_ascii('[')) return parse_class();
        if (consume_ascii('.')) return add_node(Node { .kind = NodeKind::Any });
        if (consume_ascii('^')) {
            return add_node(Node { .kind = OptionsValue.multiline ? NodeKind::AssertLineStart
                                                                  : NodeKind::AssertSubjectStart });
        }
        if (consume_ascii('$')) {
            return add_node(Node { .kind = OptionsValue.multiline ? NodeKind::AssertLineEnd
                                                                  : NodeKind::AssertSubjectEnd });
        }
        if (consume_ascii('\\')) {
            auto escaped = parse_escape(false);
            if (escaped.kind == EscapeKind::Assertion) {
                return add_node(Node { .kind = escaped.assertion });
            }
            if (escaped.kind == EscapeKind::Class) {
                auto const begin = syntax_.term_count;
                if (! add_term(escaped.term)) return INVALID_INDEX;
                return add_node(Node {
                    .kind = NodeKind::CharacterClass, .class_begin = begin, .class_count = 1 });
            }
            return add_node(Node { .kind = NodeKind::Literal, .value = escaped.literal });
        }
        if (peek_ascii('*') || peek_ascii('+') || peek_ascii('?') || peek_ascii('{')) {
            fail(RegexErrorKind::UnexpectedQuantifier, position());
            return INVALID_INDEX;
        }
        if (peek_ascii(')') || peek_ascii('|')) {
            fail(RegexErrorKind::UnexpectedToken, position());
            return INVALID_INDEX;
        }
        return add_node(Node { .kind = NodeKind::Literal, .value = read_codepoint() });
    }

    consteval auto read_decimal() noexcept -> rstd::size_t {
        rstd::size_t value {};
        while (! at_end() && rstd::ascii::is_digit(cursor_.peek()->get())) {
            value = value * 10 +
                    (cursor_.take()->get().to_primitive() - static_cast<rstd::uint8_t>('0'));
            if (value > REPEAT_LIMIT) {
                fail(RegexErrorKind::InvalidRepeat, position());
                return value;
            }
        }
        return value;
    }

    consteval auto parse_repetition() noexcept -> rstd::size_t {
        auto child = parse_atom();
        if (syntax_.error) return INVALID_INDEX;

        rstd::size_t minimum {};
        rstd::size_t maximum {};
        auto         has_repeat = true;
        if (consume_ascii('?')) {
            maximum = 1;
        } else if (consume_ascii('*')) {
            maximum = INVALID_INDEX;
        } else if (consume_ascii('+')) {
            minimum = 1;
            maximum = INVALID_INDEX;
        } else if (consume_ascii('{')) {
            auto const offset = position() - 1;
            if (at_end() || ! rstd::ascii::is_digit(cursor_.peek()->get())) {
                fail(RegexErrorKind::InvalidRepeat, offset);
                return INVALID_INDEX;
            }
            minimum = read_decimal();
            maximum = minimum;
            if (consume_ascii(',')) {
                if (peek_ascii('}')) {
                    maximum = INVALID_INDEX;
                } else if (! at_end() && rstd::ascii::is_digit(cursor_.peek()->get())) {
                    maximum = read_decimal();
                } else {
                    fail(RegexErrorKind::InvalidRepeat, offset);
                }
            }
            if (! consume_ascii('}') || (maximum != INVALID_INDEX && maximum < minimum)) {
                fail(RegexErrorKind::InvalidRepeat, offset);
                return INVALID_INDEX;
            }
        } else {
            has_repeat = false;
        }

        if (! has_repeat) return child;
        auto const greedy = ! consume_ascii('?');
        return add_node(Node { .kind    = NodeKind::Repeat,
                               .left    = child,
                               .minimum = minimum,
                               .maximum = maximum,
                               .greedy  = greedy });
    }

    consteval auto parse_concatenation() noexcept -> rstd::size_t {
        auto result = INVALID_INDEX;
        while (! syntax_.error && ! at_end() && ! peek_ascii(')') && ! peek_ascii('|')) {
            auto const next = parse_repetition();
            if (result == INVALID_INDEX) {
                result = next;
            } else {
                result = add_node(Node { .kind = NodeKind::Concat, .left = result, .right = next });
            }
        }
        if (result == INVALID_INDEX && ! syntax_.error) {
            result = add_node(Node { .kind = NodeKind::Empty });
        }
        return result;
    }

    consteval auto parse_alternation() noexcept -> rstd::size_t {
        auto result = parse_concatenation();
        while (! syntax_.error && consume_ascii('|')) {
            auto const right = parse_concatenation();
            result = add_node(Node { .kind = NodeKind::Alternate, .left = result, .right = right });
        }
        return result;
    }

public:
    consteval PatternParser() noexcept: cursor_(text_input(pattern_text())) {}

    consteval auto parse() noexcept -> SyntaxType {
        if (! str_::VALID_UTF8_LITERAL<Pattern>) {
            fail(RegexErrorKind::InvalidUtf8, 0);
            return syntax_;
        }
        syntax_.root = parse_alternation();
        if (! syntax_.error && ! at_end()) fail(RegexErrorKind::UnexpectedToken, position());
        return syntax_;
    }
};

} // namespace rstd::parse::regex
