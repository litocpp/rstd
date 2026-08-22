export module rstd.parse.regex:program;
export import :syntax;

using namespace rstd::prelude;

export namespace rstd::parse::regex
{

enum class Op : rstd::uint8_t
{
    Character,
    Any,
    CharacterClass,
    Split,
    SaveStart,
    SaveEnd,
    AssertSubjectStart,
    AssertSubjectEnd,
    AssertLineStart,
    AssertLineEnd,
    AssertWordBoundary,
    AssertNotWordBoundary,
    Accept,
};

struct Instruction {
    Op           op { Op::Accept };
    rstd::size_t out { INVALID_INDEX };
    rstd::size_t secondary { INVALID_INDEX };
    char32_t     value {};
    rstd::size_t class_begin {};
    rstd::size_t class_count {};
    rstd::size_t capture {};
    bool         negated {};
};

template<rstd::size_t PatternSize>
struct Program {
    static constexpr rstd::size_t CAPACITY = PatternSize * 8 + REPEAT_LIMIT + 64;

    Instruction  instructions[CAPACITY] {};
    rstd::size_t count {};
    rstd::size_t start { INVALID_INDEX };
};

template<rstd::size_t PatternSize>
struct FirstSet {
    bool nodes[Syntax<PatternSize>::NODE_CAPACITY] {};
};

template<rstd::size_t PatternSize>
struct LiteralPrefix {
    char32_t     values[PatternSize + 1] {};
    rstd::size_t length {};
    bool         exact {};
    bool         truncated {};
};

template<rstd::size_t PatternSize>
struct CompiledPattern {
    Syntax<PatternSize>        syntax {};
    Program<PatternSize>       program {};
    FirstSet<PatternSize>      first_set {};
    LiteralPrefix<PatternSize> literal_prefix {};
    RegexSyntaxError           error {};
    bool                       nullable {};
    bool                       subject_anchored {};
};

template<rstd::size_t PatternSize>
constexpr auto node_nullable(const Syntax<PatternSize>& syntax, rstd::size_t index) noexcept
    -> bool {
    auto const& node = syntax.nodes[index];
    switch (node.kind) {
    case NodeKind::Empty:
    case NodeKind::AssertSubjectStart:
    case NodeKind::AssertSubjectEnd:
    case NodeKind::AssertLineStart:
    case NodeKind::AssertLineEnd:
    case NodeKind::AssertWordBoundary:
    case NodeKind::AssertNotWordBoundary: return true;
    case NodeKind::Literal:
    case NodeKind::Any:
    case NodeKind::CharacterClass: return false;
    case NodeKind::Concat:
        return node_nullable(syntax, node.left) && node_nullable(syntax, node.right);
    case NodeKind::Alternate:
        return node_nullable(syntax, node.left) || node_nullable(syntax, node.right);
    case NodeKind::Repeat: return node.minimum == 0 || node_nullable(syntax, node.left);
    case NodeKind::Group: return node_nullable(syntax, node.left);
    }
    return false;
}

template<rstd::size_t PatternSize>
constexpr auto node_subject_anchored(const Syntax<PatternSize>& syntax, rstd::size_t index) noexcept
    -> bool {
    auto const& node = syntax.nodes[index];
    switch (node.kind) {
    case NodeKind::AssertSubjectStart: return true;
    case NodeKind::Concat: return node_subject_anchored(syntax, node.left);
    case NodeKind::Alternate:
        return node_subject_anchored(syntax, node.left) &&
               node_subject_anchored(syntax, node.right);
    case NodeKind::Group: return node_subject_anchored(syntax, node.left);
    default: return false;
    }
}

template<rstd::size_t PatternSize>
constexpr void collect_first_set(const Syntax<PatternSize>& syntax,
                                 rstd::size_t               index,
                                 FirstSet<PatternSize>&     result) noexcept {
    auto const& node = syntax.nodes[index];
    switch (node.kind) {
    case NodeKind::Literal:
    case NodeKind::Any:
    case NodeKind::CharacterClass: result.nodes[index] = true; break;
    case NodeKind::Concat:
        collect_first_set(syntax, node.left, result);
        if (node_nullable(syntax, node.left)) collect_first_set(syntax, node.right, result);
        break;
    case NodeKind::Alternate:
        collect_first_set(syntax, node.left, result);
        collect_first_set(syntax, node.right, result);
        break;
    case NodeKind::Repeat:
    case NodeKind::Group: collect_first_set(syntax, node.left, result); break;
    case NodeKind::Empty:
    case NodeKind::AssertSubjectStart:
    case NodeKind::AssertSubjectEnd:
    case NodeKind::AssertLineStart:
    case NodeKind::AssertLineEnd:
    case NodeKind::AssertWordBoundary:
    case NodeKind::AssertNotWordBoundary: break;
    }
}

template<rstd::size_t PatternSize>
constexpr void append_literal_prefix(LiteralPrefix<PatternSize>&       target,
                                     const LiteralPrefix<PatternSize>& source) noexcept {
    for (rstd::size_t index = 0; index < source.length; ++index) {
        if (target.length == PatternSize + 1) {
            target.truncated = true;
            break;
        }
        target.values[target.length++] = source.values[index];
    }
    target.truncated = target.truncated || source.truncated;
}

template<rstd::size_t PatternSize>
constexpr auto node_literal_prefix(const Syntax<PatternSize>& syntax, rstd::size_t index) noexcept
    -> LiteralPrefix<PatternSize> {
    auto const& node = syntax.nodes[index];
    switch (node.kind) {
    case NodeKind::Empty:
    case NodeKind::AssertSubjectStart:
    case NodeKind::AssertSubjectEnd:
    case NodeKind::AssertLineStart:
    case NodeKind::AssertLineEnd:
    case NodeKind::AssertWordBoundary:
    case NodeKind::AssertNotWordBoundary: return LiteralPrefix<PatternSize> { .exact = true };
    case NodeKind::Literal:
        return LiteralPrefix<PatternSize> { .values = { node.value }, .length = 1, .exact = true };
    case NodeKind::Any:
    case NodeKind::CharacterClass: return {};
    case NodeKind::Group: return node_literal_prefix(syntax, node.left);
    case NodeKind::Concat: {
        auto left = node_literal_prefix(syntax, node.left);
        if (! left.exact) return left;
        auto right = node_literal_prefix(syntax, node.right);
        append_literal_prefix(left, right);
        left.exact = right.exact && ! left.truncated;
        return left;
    }
    case NodeKind::Alternate: {
        auto left   = node_literal_prefix(syntax, node.left);
        auto right  = node_literal_prefix(syntax, node.right);
        auto result = LiteralPrefix<PatternSize> {};
        while (result.length < left.length && result.length < right.length &&
               left.values[result.length] == right.values[result.length]) {
            result.values[result.length] = left.values[result.length];
            ++result.length;
        }
        result.exact = left.exact && right.exact && left.length == right.length &&
                       result.length == left.length && ! left.truncated && ! right.truncated;
        return result;
    }
    case NodeKind::Repeat: {
        auto child  = node_literal_prefix(syntax, node.left);
        auto result = LiteralPrefix<PatternSize> {};
        if (! child.exact) return result;
        for (rstd::size_t count = 0; count < node.minimum; ++count) {
            append_literal_prefix(result, child);
        }
        result.exact = node.maximum == node.minimum && ! result.truncated;
        return result;
    }
    }
    return {};
}

template<rstd::size_t PatternSize>
class ProgramCompiler {
    CompiledPattern<PatternSize>& compiled_;

    consteval void fail(RegexErrorKind kind) noexcept {
        if (! compiled_.error) compiled_.error = RegexSyntaxError { kind, PatternSize };
    }

    consteval auto emit(Instruction instruction) noexcept -> rstd::size_t {
        auto& program = compiled_.program;
        if (program.count >= Program<PatternSize>::CAPACITY) {
            fail(RegexErrorKind::ProgramTooLarge);
            return INVALID_INDEX;
        }
        auto const index            = program.count++;
        program.instructions[index] = instruction;
        return index;
    }

    consteval auto compile_repeat(const Node& node, rstd::size_t next) noexcept -> rstd::size_t {
        auto result = next;
        if (node.maximum == INVALID_INDEX) {
            auto const split = emit(Instruction { .op = Op::Split });
            if (compiled_.error) return INVALID_INDEX;
            auto const child = compile_node(node.left, split);
            if (compiled_.error) return INVALID_INDEX;
            if (node.greedy) {
                compiled_.program.instructions[split].out       = child;
                compiled_.program.instructions[split].secondary = next;
            } else {
                compiled_.program.instructions[split].out       = next;
                compiled_.program.instructions[split].secondary = child;
            }
            result = split;
        } else {
            for (rstd::size_t count = node.maximum; count > node.minimum; --count) {
                auto const child = compile_node(node.left, result);
                result =
                    node.greedy
                        ? emit(Instruction { .op = Op::Split, .out = child, .secondary = result })
                        : emit(Instruction { .op = Op::Split, .out = result, .secondary = child });
            }
        }
        for (rstd::size_t count = 0; count < node.minimum; ++count) {
            result = compile_node(node.left, result);
        }
        return result;
    }

    consteval auto compile_node(rstd::size_t index, rstd::size_t next) noexcept -> rstd::size_t {
        if (compiled_.error) return INVALID_INDEX;
        auto const& node = compiled_.syntax.nodes[index];
        switch (node.kind) {
        case NodeKind::Empty: return next;
        case NodeKind::Literal:
            return emit(Instruction { .op = Op::Character, .out = next, .value = node.value });
        case NodeKind::Any: return emit(Instruction { .op = Op::Any, .out = next });
        case NodeKind::CharacterClass:
            return emit(Instruction { .op          = Op::CharacterClass,
                                      .out         = next,
                                      .class_begin = node.class_begin,
                                      .class_count = node.class_count,
                                      .negated     = node.negated });
        case NodeKind::Concat: return compile_node(node.left, compile_node(node.right, next));
        case NodeKind::Alternate: {
            auto const left  = compile_node(node.left, next);
            auto const right = compile_node(node.right, next);
            return emit(Instruction { .op = Op::Split, .out = left, .secondary = right });
        }
        case NodeKind::Repeat: return compile_repeat(node, next);
        case NodeKind::Group: {
            auto const end =
                emit(Instruction { .op = Op::SaveEnd, .out = next, .capture = node.capture });
            auto const child = compile_node(node.left, end);
            return emit(Instruction { .op = Op::SaveStart, .out = child, .capture = node.capture });
        }
        case NodeKind::AssertSubjectStart:
            return emit(Instruction { .op = Op::AssertSubjectStart, .out = next });
        case NodeKind::AssertSubjectEnd:
            return emit(Instruction { .op = Op::AssertSubjectEnd, .out = next });
        case NodeKind::AssertLineStart:
            return emit(Instruction { .op = Op::AssertLineStart, .out = next });
        case NodeKind::AssertLineEnd:
            return emit(Instruction { .op = Op::AssertLineEnd, .out = next });
        case NodeKind::AssertWordBoundary:
            return emit(Instruction { .op = Op::AssertWordBoundary, .out = next });
        case NodeKind::AssertNotWordBoundary:
            return emit(Instruction { .op = Op::AssertNotWordBoundary, .out = next });
        }
        return INVALID_INDEX;
    }

public:
    consteval explicit ProgramCompiler(CompiledPattern<PatternSize>& compiled) noexcept
        : compiled_(compiled) {}

    consteval void compile() noexcept {
        auto const accept = emit(Instruction { .op = Op::Accept });
        auto const end    = emit(Instruction { .op = Op::SaveEnd, .out = accept, .capture = 0 });
        auto const body   = compile_node(compiled_.syntax.root, end);
        compiled_.program.start =
            emit(Instruction { .op = Op::SaveStart, .out = body, .capture = 0 });
    }
};

template<str_::fixed_string Pattern, Options OptionsValue>
consteval auto build_compiled_pattern() noexcept -> CompiledPattern<Pattern.size()> {
    auto result   = CompiledPattern<Pattern.size()> {};
    result.syntax = PatternParser<Pattern, OptionsValue> {}.parse();
    if (result.syntax.error) {
        result.error = result.syntax.error;
        return result;
    }
    result.nullable         = node_nullable(result.syntax, result.syntax.root);
    result.subject_anchored = node_subject_anchored(result.syntax, result.syntax.root);
    collect_first_set(result.syntax, result.syntax.root, result.first_set);
    if constexpr (! OptionsValue.case_insensitive) {
        result.literal_prefix = node_literal_prefix(result.syntax, result.syntax.root);
    }
    ProgramCompiler<Pattern.size()> compiler(result);
    compiler.compile();
    return result;
}

template<str_::fixed_string Pattern, Options OptionsValue>
inline constexpr auto COMPILED_PATTERN = build_compiled_pattern<Pattern, OptionsValue>();

template<rstd::size_t PatternSize, bool TrackCaptures>
struct CaptureState {};

template<rstd::size_t PatternSize>
struct CaptureState<PatternSize, true> {
    static constexpr rstd::size_t SLOT_COUNT = Syntax<PatternSize>::CAPTURE_CAPACITY * 2;
    rstd::size_t                  slots[SLOT_COUNT] {};

    constexpr CaptureState() noexcept {
        for (auto& slot : slots) slot = INVALID_INDEX;
    }
};

template<rstd::size_t PatternSize, bool TrackCaptures>
struct Thread : CaptureState<PatternSize, TrackCaptures> {
    rstd::size_t pc {};
    rstd::size_t start {};
};

template<rstd::size_t PatternSize, bool TrackCaptures>
struct ThreadList {
    static constexpr rstd::size_t CAPACITY = Program<PatternSize>::CAPACITY;

    Thread<PatternSize, TrackCaptures> threads[CAPACITY] {};
    bool                               seen[CAPACITY] {};
    rstd::size_t                       count {};
};

template<rstd::size_t PatternSize, bool TrackCaptures>
struct RawResult : CaptureState<PatternSize, TrackCaptures> {
    bool         matched {};
    rstd::size_t begin {};
    rstd::size_t end {};
};

struct ExecutionMetrics {
    rstd::size_t instruction_visits {};
    rstd::size_t maximum_threads {};
};

constexpr auto ascii_fold(char32_t value) noexcept -> char32_t {
    if (value >= U'A' && value <= U'Z') return value + (U'a' - U'A');
    return value;
}

constexpr auto is_ascii_word(char32_t value) noexcept -> bool {
    if (value > 0x7f) return false;
    return rstd::ascii::is_alnum(u8(static_cast<rstd::uint8_t>(value))) || value == U'_';
}

constexpr auto is_ascii_space(char32_t value) noexcept -> bool {
    return value <= 0x7f && rstd::ascii::is_space(u8(static_cast<rstd::uint8_t>(value)));
}

template<Options OptionsValue>
constexpr auto match_range(char32_t value, char32_t low, char32_t high) noexcept -> bool {
    if (value >= low && value <= high) return true;
    if constexpr (OptionsValue.case_insensitive) {
        auto const folded = ascii_fold(value);
        return folded >= ascii_fold(low) && folded <= ascii_fold(high);
    }
    return false;
}

template<rstd::size_t PatternSize, Options OptionsValue>
constexpr auto matches_class(const CompiledPattern<PatternSize>& compiled,
                             const Instruction&                  instruction,
                             char32_t                            value) noexcept -> bool {
    auto matched = false;
    for (rstd::size_t index = 0; index < instruction.class_count; ++index) {
        auto const& term       = compiled.syntax.terms[instruction.class_begin + index];
        auto        term_match = false;
        switch (term.kind) {
        case ClassTermKind::Range:
            term_match = match_range<OptionsValue>(value, term.low, term.high);
            break;
        case ClassTermKind::Digit: term_match = value >= U'0' && value <= U'9'; break;
        case ClassTermKind::Space: term_match = is_ascii_space(value); break;
        case ClassTermKind::Word: term_match = is_ascii_word(value); break;
        }
        if (term.negated) term_match = ! term_match;
        matched = matched || term_match;
    }
    return instruction.negated ? ! matched : matched;
}

constexpr auto assertion_word_at(ref<str> input, rstd::size_t position) noexcept -> bool {
    if (position >= input.len().to_primitive()) return false;
    auto const byte = input[usize(position)].to_primitive();
    return byte <= 0x7f && is_ascii_word(static_cast<char32_t>(byte));
}

constexpr auto assertion_word_before(ref<str> input, rstd::size_t position) noexcept -> bool {
    if (position == 0) return false;
    auto const byte = input[usize(position - 1)].to_primitive();
    return byte <= 0x7f && is_ascii_word(static_cast<char32_t>(byte));
}

template<str_::fixed_string Pattern, Options OptionsValue, bool TrackCaptures>
constexpr void add_epsilon(ThreadList<Pattern.size(), TrackCaptures>& list,
                           Thread<Pattern.size(), TrackCaptures>      thread,
                           rstd::size_t                               pc,
                           rstd::size_t                               position,
                           ref<str>                                   input,
                           ExecutionMetrics*                          metrics) noexcept {
    using ThreadType = Thread<Pattern.size(), TrackCaptures>;
    struct Pending {
        ThreadType   thread {};
        rstd::size_t pc {};
    };

    constexpr auto PENDING_CAPACITY = Program<Pattern.size()>::CAPACITY * 2;
    Pending        pending[PENDING_CAPACITY] {};
    rstd::size_t   pending_count {};
    pending[pending_count++] = Pending { thread, pc };

    auto const& compiled = COMPILED_PATTERN<Pattern, OptionsValue>;
    while (pending_count != 0) {
        auto current = pending[--pending_count];
        if (current.pc == INVALID_INDEX || current.pc >= compiled.program.count ||
            list.seen[current.pc]) {
            continue;
        }
        if (metrics != nullptr) ++metrics->instruction_visits;
        list.seen[current.pc]   = true;
        auto const& instruction = compiled.program.instructions[current.pc];
        switch (instruction.op) {
        case Op::Split:
            pending[pending_count++] = Pending { current.thread, instruction.secondary };
            pending[pending_count++] = Pending { current.thread, instruction.out };
            break;
        case Op::SaveStart:
            if constexpr (TrackCaptures) {
                current.thread.slots[instruction.capture * 2] = position;
            }
            pending[pending_count++] = Pending { current.thread, instruction.out };
            break;
        case Op::SaveEnd:
            if constexpr (TrackCaptures) {
                current.thread.slots[instruction.capture * 2 + 1] = position;
            }
            pending[pending_count++] = Pending { current.thread, instruction.out };
            break;
        case Op::AssertSubjectStart:
            if (position == 0) {
                pending[pending_count++] = Pending { current.thread, instruction.out };
            }
            break;
        case Op::AssertSubjectEnd:
            if (position == input.len().to_primitive()) {
                pending[pending_count++] = Pending { current.thread, instruction.out };
            }
            break;
        case Op::AssertLineStart:
            if (position == 0 || input[usize(position - 1)] == u8('\n')) {
                pending[pending_count++] = Pending { current.thread, instruction.out };
            }
            break;
        case Op::AssertLineEnd:
            if (position == input.len().to_primitive() || input[usize(position)] == u8('\n')) {
                pending[pending_count++] = Pending { current.thread, instruction.out };
            }
            break;
        case Op::AssertWordBoundary:
            if (assertion_word_before(input, position) != assertion_word_at(input, position)) {
                pending[pending_count++] = Pending { current.thread, instruction.out };
            }
            break;
        case Op::AssertNotWordBoundary:
            if (assertion_word_before(input, position) == assertion_word_at(input, position)) {
                pending[pending_count++] = Pending { current.thread, instruction.out };
            }
            break;
        default:
            current.thread.pc          = current.pc;
            list.threads[list.count++] = current.thread;
            break;
        }
    }
    if (metrics != nullptr && list.count > metrics->maximum_threads) {
        metrics->maximum_threads = list.count;
    }
}

enum class RunMode : rstd::uint8_t
{
    Search,
    Prefix,
    Full
};

template<rstd::size_t PatternSize, bool TrackCaptures>
constexpr auto raw_result(const Thread<PatternSize, TrackCaptures>& thread,
                          rstd::size_t end) noexcept -> RawResult<PatternSize, TrackCaptures> {
    auto result    = RawResult<PatternSize, TrackCaptures> {};
    result.matched = true;
    result.end     = end;
    if constexpr (TrackCaptures) {
        auto const slots = Syntax<PatternSize>::CAPTURE_CAPACITY * 2;
        for (rstd::size_t index = 0; index < slots; ++index) {
            result.slots[index] = thread.slots[index];
        }
        result.begin = result.slots[0];
    } else {
        result.begin = thread.start;
    }
    return result;
}

template<str_::fixed_string Pattern, Options OptionsValue, bool TrackCaptures>
constexpr auto
run(ref<str> input, rstd::size_t from, RunMode mode, ExecutionMetrics* metrics = nullptr) noexcept
    -> RawResult<Pattern.size(), TrackCaptures> {
    using List            = ThreadList<Pattern.size(), TrackCaptures>;
    using ThreadType      = Thread<Pattern.size(), TrackCaptures>;
    auto const& compiled  = COMPILED_PATTERN<Pattern, OptionsValue>;
    auto        current   = List {};
    auto        candidate = RawResult<Pattern.size(), TrackCaptures> {};
    auto        position  = from;
    auto        inject    = true;

    while (position <= input.len().to_primitive()) {
        if (inject && (mode != RunMode::Search || ! candidate.matched)) {
            if (! compiled.subject_anchored || position == 0) {
                auto thread  = ThreadType {};
                thread.start = position;
                add_epsilon<Pattern, OptionsValue>(
                    current, thread, compiled.program.start, position, input, metrics);
            }
            inject = mode == RunMode::Search;
        }

        if (mode == RunMode::Full) {
            if (position == input.len().to_primitive()) {
                for (rstd::size_t index = 0; index < current.count; ++index) {
                    if (compiled.program.instructions[current.threads[index].pc].op == Op::Accept) {
                        return raw_result(current.threads[index], position);
                    }
                }
                return {};
            }
        } else {
            for (rstd::size_t index = 0; index < current.count; ++index) {
                if (compiled.program.instructions[current.threads[index].pc].op == Op::Accept) {
                    candidate     = raw_result(current.threads[index], position);
                    current.count = index;
                    break;
                }
            }
            if (candidate.matched && current.count == 0) return candidate;
            if (position == input.len().to_primitive()) return candidate;
        }

        auto const remaining = input.get(usize(position), input.len()).unwrap_unchecked();
        auto       indices   = remaining.char_indices();
        auto const code_point =
            static_cast<char32_t>(get<1>(indices.next().unwrap_unchecked()).to_primitive());
        auto const next_position =
            input.len().to_primitive() - indices.as_str().len().to_primitive();
        auto next = List {};
        for (rstd::size_t index = 0; index < current.count; ++index) {
            auto const thread      = current.threads[index];
            auto const instruction = compiled.program.instructions[thread.pc];
            if (metrics != nullptr) ++metrics->instruction_visits;
            auto matched = false;
            switch (instruction.op) {
            case Op::Character:
                matched = OptionsValue.case_insensitive
                              ? ascii_fold(code_point) == ascii_fold(instruction.value)
                              : code_point == instruction.value;
                break;
            case Op::Any: matched = OptionsValue.dot_matches_newline || code_point != U'\n'; break;
            case Op::CharacterClass:
                matched =
                    matches_class<Pattern.size(), OptionsValue>(compiled, instruction, code_point);
                break;
            default: break;
            }
            if (matched) {
                add_epsilon<Pattern, OptionsValue>(
                    next, thread, instruction.out, next_position, input, metrics);
            }
        }

        if (candidate.matched && next.count == 0) return candidate;
        if (mode != RunMode::Search && ! candidate.matched && next.count == 0) return {};
        current  = next;
        position = next_position;
    }
    return candidate;
}

} // namespace rstd::parse::regex

namespace rstd::parse::regex
{

template<str_::fixed_string Input>
consteval auto ambiguous_repeat_audit() noexcept {
    constexpr auto& storage = str_::BYTE_LITERAL_STORAGE<Input>;
    auto input   = ref<str>::from_raw_parts_unchecked(storage.data(), usize(storage.size()));
    auto metrics = ExecutionMetrics {};
    auto result  = run<"(a|aa)*b", Options {}, false>(input, 0, RunMode::Full, &metrics);
    return tuple(metrics, result.matched);
}

consteval auto ambiguous_repeat_is_bounded() noexcept -> bool {
    auto short_run     = ambiguous_repeat_audit<"aaaaaaaaaaaaaaaab">();
    auto long_run      = ambiguous_repeat_audit<"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab">();
    auto short_metrics = get<0>(short_run);
    auto long_metrics  = get<0>(long_run);
    return get<1>(short_run) && get<1>(long_run) &&
           long_metrics.instruction_visits <= short_metrics.instruction_visits * 2 &&
           long_metrics.maximum_threads <= short_metrics.maximum_threads;
}

static_assert(ambiguous_repeat_is_bounded());

} // namespace rstd::parse::regex

export namespace rstd::parse::regex
{

template<RegexErrorKind Kind, rstd::size_t ByteOffset>
struct RegexDiagnostic {};

template<typename>
inline constexpr bool REGEX_DEPENDENT_FALSE = false;

template<RegexErrorKind Kind, rstd::size_t ByteOffset>
consteval void require_valid_pattern() {
    if constexpr (Kind != RegexErrorKind::None) {
        static_assert(
            REGEX_DEPENDENT_FALSE<RegexDiagnostic<Kind, ByteOffset>>,
            "rstd regex syntax error; RegexDiagnostic arguments contain kind and byte offset");
    }
}

} // namespace rstd::parse::regex
