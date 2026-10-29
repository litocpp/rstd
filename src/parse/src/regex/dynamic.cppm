export module rstd.parse.regex:dynamic;
import :program;
import rstd.alloc;
using namespace rstd::prelude;
using ::alloc::vec::Vec;
using ::alloc::boxed::Box;
using ::alloc::sync::SharedAllocator;
export namespace rstd::parse::regex
{
class RegexScratch {
    Vec<usize, SharedAllocator> current_, next_, pending_;
    Vec<u8, SharedAllocator>    seen_;
    friend class DynamicRegex;

public:
    explicit RegexScratch(SharedAllocator allocator = {})
        : current_(decltype(current_)::make_in(allocator.clone())),
          next_(decltype(next_)::make_in(allocator.clone())),
          pending_(decltype(pending_)::make_in(allocator.clone())),
          seen_(decltype(seen_)::make_in(rstd::move(allocator))) {}
};
class DynamicRegex {
    Vec<Instruction, SharedAllocator> instructions_;
    Vec<ClassTerm, SharedAllocator>   terms_;
    explicit DynamicRegex(SharedAllocator allocator)
        : instructions_(decltype(instructions_)::make_in(allocator.clone())),
          terms_(decltype(terms_)::make_in(rstd::move(allocator))) {}
    usize start_;
    void  expand(RegexScratch& scratch, usize pc, ref<str> text, usize offset) const {
        scratch.pending_.clear();
        scratch.pending_.push(usize(pc));
        while (! scratch.pending_.is_empty()) {
            auto at = scratch.pending_.pop().unwrap();
            if (scratch.seen_[at] != u8()) continue;
            scratch.seen_[at]  = u8(1);
            const auto& inst   = instructions_[at];
            bool        follow = false;
            switch (inst.op) {
            case Op::Split:
                scratch.pending_.push(usize(inst.secondary));
                follow = true;
                break;
            case Op::SaveStart:
            case Op::SaveEnd: follow = true; break;
            case Op::AssertSubjectStart: follow = offset == usize(); break;
            case Op::AssertSubjectEnd: follow = offset == text.len(); break;
            case Op::AssertLineStart:
                follow = offset == usize() || text[offset - usize(1)] == u8('\n');
                break;
            case Op::AssertLineEnd:
                follow = offset == text.len() || text[offset] == u8('\n');
                break;
            case Op::AssertWordBoundary:
            case Op::AssertNotWordBoundary:
                follow = (assertion_word_at(text, offset.to_primitive()) !=
                          assertion_word_before(text, offset.to_primitive())) ==
                         (inst.op == Op::AssertWordBoundary);
                break;
            default: scratch.next_.push(usize(at)); break;
            }
            if (follow) scratch.pending_.push(usize(inst.out));
        }
    }
    auto accepts(const Instruction& inst, char32_t value) const -> bool {
        if (inst.op == Op::Character) return value == inst.value;
        if (inst.op == Op::Any) return value != U'\n';
        if (inst.op != Op::CharacterClass) return false;
        bool matched = false;
        for (rstd::size_t i = 0; i < inst.class_count; ++i) {
            const auto& term = terms_[usize(inst.class_begin + i)];
            bool        hit  = false;
            switch (term.kind) {
            case ClassTermKind::Range: hit = value >= term.low && value <= term.high; break;
            case ClassTermKind::Digit: hit = value >= U'0' && value <= U'9'; break;
            case ClassTermKind::Space: hit = is_ascii_space(value); break;
            case ClassTermKind::Word: hit = is_ascii_word(value); break;
            }
            matched = matched || (term.negated ? ! hit : hit);
        }
        return inst.negated ? ! matched : matched;
    }

public:
    static constexpr usize MAXIMUM_PATTERN_BYTES { 1024 };
    static auto            compile(ref<str> pattern, SharedAllocator allocator = {})
        -> Result<DynamicRegex, RegexSyntaxError> {
        if (pattern.len() > MAXIMUM_PATTERN_BYTES)
            return Err(
                RegexSyntaxError { RegexErrorKind::ProgramTooLarge, pattern.len().to_primitive() });
        auto compiled    = Box<CompiledPattern<1024>, SharedAllocator>::make_in(allocator.clone());
        compiled->syntax = SyntaxParser<1024, Options {}>(pattern).parse();
        if (compiled->syntax.error) return Err(compiled->syntax.error);
        ProgramCompiler<1024>(*compiled).compile();
        if (compiled->error) return Err(compiled->error);
        DynamicRegex out(rstd::move(allocator));
        out.start_ = usize(compiled->program.start);
        out.instructions_.reserve_exact(usize(compiled->program.count));
        out.terms_.reserve_exact(usize(compiled->syntax.term_count));
        for (rstd::size_t i = 0; i < compiled->program.count; ++i)
            out.instructions_.push(Instruction(compiled->program.instructions[i]));
        for (rstd::size_t i = 0; i < compiled->syntax.term_count; ++i)
            out.terms_.push(ClassTerm(compiled->syntax.terms[i]));
        return Ok(rstd::move(out));
    }
    auto allocation_size() const -> usize {
        return instructions_.capacity() * usize(sizeof(Instruction)) +
               terms_.capacity() * usize(sizeof(ClassTerm));
    }
    auto is_match(ref<str> input, RegexScratch& scratch) const -> bool {
        scratch.current_.clear();
        scratch.next_.clear();
        scratch.seen_.resize(instructions_.len(), u8());
        for (usize i; i < scratch.seen_.len(); ++i) scratch.seen_[i] = u8();
        TextCursor cursor(text_input(input));
        for (;;) {
            expand(scratch, start_, input, cursor.position());
            for (auto pc : scratch.next_)
                if (instructions_[pc].op == Op::Accept) return true;
            if (cursor.is_eof()) return false;
            auto remaining = cursor.remaining_text();
            if (remaining.is_err()) return false;
            auto indices = remaining->char_indices();
            auto point   = get<1>(indices.next().unwrap());
            (void)cursor.advance(remaining->len() - indices.as_str().len());
            rstd::mem::swap(scratch.current_, scratch.next_);
            scratch.next_.clear();
            for (usize i; i < scratch.seen_.len(); ++i) scratch.seen_[i] = u8();
            for (auto pc : scratch.current_) {
                const auto& inst = instructions_[pc];
                if (accepts(inst, static_cast<char32_t>(point.to_primitive())))
                    expand(scratch, usize(inst.out), input, cursor.position());
            }
        }
    }
};
} // namespace rstd::parse::regex
