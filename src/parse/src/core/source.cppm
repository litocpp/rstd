export module rstd.parse.core:source;
export import rstd.core;

using namespace rstd::prelude;

export namespace rstd::parse
{

struct Span {
    usize begin {};
    usize end {};

    constexpr auto is_empty() const noexcept -> bool { return begin == end; }
    constexpr auto len() const noexcept -> usize { return end - begin; }
    constexpr auto operator==(const Span&) const noexcept -> bool = default;
};

struct SourcePosition {
    usize line { 1 };
    usize column { 1 };

    constexpr auto operator==(const SourcePosition&) const noexcept -> bool = default;
};

class RuleId {
    ref<str> name_;

public:
    constexpr explicit RuleId(ref<str> name) noexcept: name_(name) {}

    constexpr auto name() const noexcept [[clang::lifetimebound]] -> ref<str> { return name_; }
    constexpr auto operator==(const RuleId&) const noexcept -> bool = default;
};

template<typename T>
struct InputTextState {};

template<>
struct InputTextState<u8> {
    bool validated {};
};

template<typename T>
class Input {
    slice<T> values_;
    [[no_unique_address]]
    InputTextState<T> text_;

public:
    constexpr explicit Input(slice<T> values) noexcept: values_(values) {}

    constexpr auto values() const noexcept [[clang::lifetimebound]] -> slice<T> { return values_; }
    constexpr auto len() const noexcept -> usize { return values_.len(); }
    constexpr auto is_empty() const noexcept -> bool { return values_.is_empty(); }

    static constexpr auto from_text(ref<str> text) noexcept -> Input
        requires mtp::same_as<T, u8>
    {
        auto result            = Input(text.as_bytes());
        result.text_.validated = true;
        return result;
    }

    constexpr auto text(Span span) const noexcept [[clang::lifetimebound]]
    -> Result<ref<str>, str_::Utf8Error>
        requires mtp::same_as<T, u8>
    {
        if (span.begin > span.end || span.end > len()) rstd::panic("invalid parse span");
        auto bytes = span.is_empty()
                         ? slice<u8> {}
                         : slice<u8>::from_raw_parts(
                               values_.as_raw_ptr() + span.begin.to_primitive(), span.len());
        if (text_.validated) {
            auto text     = str_::from_utf8_unchecked(values_);
            auto selected = text.get(span.begin, span.end);
            if (selected.is_some()) return Ok(*selected);
            // Even an empty byte range may split a code point.
            if (span.is_empty()) return Err(str_::Utf8Error(usize(), None()));
        }
        return str_::from_utf8(bytes);
    }
};

using TextInput = Input<u8>;

constexpr auto text_input(ref<str> input) noexcept -> TextInput {
    return TextInput::from_text(input);
}

struct UntrackedPosition {
    template<typename T>
    constexpr void advance(slice<T>) noexcept {}
};

class LinePosition {
    usize line_ { 1 };
    usize line_start_ {};
    bool  carriage_return_ {};

public:
    constexpr auto position(usize offset) const noexcept -> SourcePosition {
        return { line_, offset - line_start_ + usize(1) };
    }
    constexpr void advance(slice<u8> bytes, usize start) noexcept {
        for (usize index {}; index < bytes.len(); ++index) {
            auto value  = bytes[index];
            auto offset = start + index;
            if (value == u8('\r')) {
                ++line_;
                line_start_      = offset + usize(1);
                carriage_return_ = true;
            } else if (value == u8('\n')) {
                if (! carriage_return_ || line_start_ != offset) ++line_;
                line_start_      = offset + usize(1);
                carriage_return_ = false;
            }
        }
    }
};

template<typename Position>
class CursorCheckpoint {
    const void* owner_ {};
    usize       position_ {};
    [[no_unique_address]]
    Position tracking_;

    constexpr CursorCheckpoint(const void* owner, usize position, Position tracking) noexcept
        : owner_(owner), position_(position), tracking_(tracking) {}

    template<typename, typename>
    friend class Cursor;
};

using Checkpoint = CursorCheckpoint<UntrackedPosition>;

template<typename T, typename Position = UntrackedPosition>
class Cursor {
    Input<T> input_;
    usize    position_ {};
    usize    furthest_ {};
    [[no_unique_address]]
    Position tracking_;

public:
    using checkpoint_type = CursorCheckpoint<Position>;
    constexpr explicit Cursor(Input<T> input) noexcept: input_(input), tracking_() {}
    Cursor(const Cursor&)                    = delete;
    Cursor(Cursor&&)                         = delete;
    auto operator=(const Cursor&) -> Cursor& = delete;
    auto operator=(Cursor&&) -> Cursor&      = delete;

    constexpr auto input() const noexcept [[clang::lifetimebound]] -> slice<T> {
        return input_.values();
    }
    constexpr auto position() const noexcept -> usize { return position_; }
    constexpr auto furthest_position() const noexcept -> usize {
        return position_ > furthest_ ? position_ : furthest_;
    }
    constexpr auto len() const noexcept -> usize { return input_.len(); }
    constexpr auto remaining() const noexcept -> usize { return len() - position_; }
    constexpr auto is_eof() const noexcept -> bool { return position_ == input_.values().len(); }

    constexpr auto remaining_input() const noexcept [[clang::lifetimebound]] -> slice<T> {
        if (is_eof()) return {};
        return slice<T>::from_raw_parts(input_.values().as_raw_ptr() + position_.to_primitive(),
                                        remaining());
    }

    constexpr auto checkpoint() const noexcept -> checkpoint_type {
        return checkpoint_type(this, position_, tracking_);
    }

    constexpr void rewind(checkpoint_type checkpoint) noexcept {
        if (checkpoint.owner_ != this || checkpoint.position_ > input_.values().len()) {
            rstd::panic("parse checkpoint belongs to another cursor");
        }
        furthest_ = furthest_position();
        position_ = checkpoint.position_;
        tracking_ = checkpoint.tracking_;
    }

    constexpr auto span_from(checkpoint_type checkpoint) const noexcept -> Span {
        if (checkpoint.owner_ != this || checkpoint.position_ > position_) {
            rstd::panic("invalid parse checkpoint span");
        }
        return Span { .begin = checkpoint.position_, .end = position_ };
    }

    constexpr auto view(Span span) const noexcept [[clang::lifetimebound]] -> slice<T> {
        if (span.begin > span.end || span.end > len()) rstd::panic("invalid parse span");
        if (span.is_empty()) return {};
        return slice<T>::from_raw_parts(input_.values().as_raw_ptr() + span.begin.to_primitive(),
                                        span.len());
    }

    constexpr auto consumed(checkpoint_type checkpoint) const noexcept [[clang::lifetimebound]]
    -> slice<T> {
        return view(span_from(checkpoint));
    }

    constexpr auto peek(usize ahead = usize()) const noexcept -> Option<ref<T>> {
        if (ahead >= remaining()) return None();
        return Some(ref<T>::from_raw_parts(input_.values().as_raw_ptr() + position_.to_primitive() +
                                           ahead.to_primitive()));
    }

    constexpr auto take() noexcept -> Option<ref<T>> {
        auto value = peek();
        if (value.is_some()) {
            (void)advance(usize(1));
        }
        return value;
    }

    constexpr auto advance(usize count) noexcept -> bool {
        if (count > remaining()) return false;
        if (count == usize()) return true;
        if constexpr (! mtp::same_as<Position, UntrackedPosition>) {
            tracking_.advance(slice<T>::from_raw_parts(
                                  input_.values().as_raw_ptr() + position_.to_primitive(), count),
                              position_);
        }
        position_ += count;
        return true;
    }

    // The consumed bytes must contain neither CR nor LF; bounds remain checked.
    constexpr auto advance_single_line_unchecked(usize count) noexcept -> bool
        requires(mtp::same_as<T, u8> && mtp::same_as<Position, LinePosition>)
    {
        if (count > remaining()) return false;
        position_ += count;
        return true;
    }

    constexpr auto source_position(usize offset) const noexcept -> SourcePosition
        requires mtp::same_as<T, u8>
    {
        if (offset > input_.values().len()) rstd::panic("parse source offset is out of bounds");
        if constexpr (mtp::same_as<Position, LinePosition>) {
            auto result = LinePosition {};
            result.advance(view(Span { usize(), offset }), usize());
            return result.position(offset);
        } else {
            // Preserve LF-only diagnostics for existing untracked text parsers.
            auto result = SourcePosition {};
            for (usize index {}; index < offset; ++index) {
                if (input_.values()[index] == u8('\n')) {
                    ++result.line;
                    result.column = usize(1);
                } else {
                    ++result.column;
                }
            }
            return result;
        }
    }

    constexpr auto source_position() const noexcept -> SourcePosition
        requires mtp::same_as<T, u8>
    {
        if constexpr (mtp::same_as<Position, LinePosition>)
            return tracking_.position(position_);
        else
            return source_position(position_);
    }

    constexpr auto text(Span span) const noexcept [[clang::lifetimebound]]
    -> Result<ref<str>, str_::Utf8Error>
        requires mtp::same_as<T, u8>
    {
        return input_.text(span);
    }

    constexpr auto consumed_text(checkpoint_type checkpoint) const noexcept [[clang::lifetimebound]]
    -> Result<ref<str>, str_::Utf8Error>
        requires mtp::same_as<T, u8>
    {
        return text(span_from(checkpoint));
    }

    constexpr auto remaining_text() const noexcept [[clang::lifetimebound]]
    -> Result<ref<str>, str_::Utf8Error>
        requires mtp::same_as<T, u8>
    {
        return text(Span { position_, len() });
    }
};

using TextCursor       = Cursor<u8>;
using PositionedCursor = Cursor<u8, LinePosition>;

} // namespace rstd::parse
