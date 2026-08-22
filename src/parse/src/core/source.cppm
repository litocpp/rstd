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
class Input {
    slice<T> values_;

public:
    constexpr explicit Input(slice<T> values) noexcept: values_(values) {}

    constexpr auto values() const noexcept [[clang::lifetimebound]] -> slice<T> { return values_; }
    constexpr auto len() const noexcept -> usize { return values_.len(); }
    constexpr auto is_empty() const noexcept -> bool { return values_.is_empty(); }
};

using TextInput = Input<u8>;

constexpr auto text_input(ref<str> input) noexcept -> TextInput {
    return TextInput(input.as_bytes());
}

class Checkpoint {
    const void* owner_ {};
    usize       position_ {};

    constexpr Checkpoint(const void* owner, usize position) noexcept
        : owner_(owner), position_(position) {}

    template<typename>
    friend class Cursor;
};

template<typename T>
class Cursor {
    Input<T> input_;
    usize    position_ {};
    usize    furthest_ {};

public:
    constexpr explicit Cursor(Input<T> input) noexcept: input_(input) {}
    Cursor(const Cursor&)                    = delete;
    Cursor(Cursor&&)                         = delete;
    auto operator=(const Cursor&) -> Cursor& = delete;
    auto operator=(Cursor&&) -> Cursor&      = delete;

    constexpr auto input() const noexcept [[clang::lifetimebound]] -> slice<T> {
        return input_.values();
    }
    constexpr auto position() const noexcept -> usize { return position_; }
    constexpr auto furthest_position() const noexcept -> usize { return furthest_; }
    constexpr auto len() const noexcept -> usize { return input_.len(); }
    constexpr auto remaining() const noexcept -> usize { return len() - position_; }
    constexpr auto is_eof() const noexcept -> bool { return position_ == input_.values().len(); }

    constexpr auto remaining_input() const noexcept [[clang::lifetimebound]] -> slice<T> {
        if (is_eof()) return {};
        return slice<T>::from_raw_parts(input_.values().as_raw_ptr() + position_.to_primitive(),
                                        remaining());
    }

    constexpr auto checkpoint() const noexcept -> Checkpoint { return Checkpoint(this, position_); }

    constexpr void rewind(Checkpoint checkpoint) noexcept {
        if (checkpoint.owner_ != this || checkpoint.position_ > input_.values().len()) {
            rstd::panic("parse checkpoint belongs to another cursor");
        }
        position_ = checkpoint.position_;
    }

    constexpr auto span_from(Checkpoint checkpoint) const noexcept -> Span {
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

    constexpr auto consumed(Checkpoint checkpoint) const noexcept [[clang::lifetimebound]]
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
            ++position_;
            if (position_ > furthest_) furthest_ = position_;
        }
        return value;
    }

    constexpr auto advance(usize count) noexcept -> bool {
        if (count > remaining()) return false;
        position_ += count;
        if (position_ > furthest_) furthest_ = position_;
        return true;
    }

    constexpr auto source_position(usize offset) const noexcept -> SourcePosition
        requires mtp::same_as<T, u8>
    {
        if (offset > input_.values().len()) rstd::panic("parse source offset is out of bounds");
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

    constexpr auto source_position() const noexcept -> SourcePosition
        requires mtp::same_as<T, u8>
    {
        return source_position(position_);
    }

    constexpr auto text(Span span) const noexcept [[clang::lifetimebound]] -> ref<str>
        requires mtp::same_as<T, u8>
    {
        auto bytes = view(span);
        return ref<str>::from_raw_parts_unchecked(bytes.as_raw_ptr(), bytes.len());
    }

    constexpr auto consumed_text(Checkpoint checkpoint) const noexcept [[clang::lifetimebound]]
    -> ref<str>
        requires mtp::same_as<T, u8>
    {
        return text(span_from(checkpoint));
    }

    constexpr auto remaining_text() const noexcept [[clang::lifetimebound]] -> ref<str>
        requires mtp::same_as<T, u8>
    {
        auto bytes = remaining_input();
        return ref<str>::from_raw_parts_unchecked(bytes.as_raw_ptr(), bytes.len());
    }
};

using TextCursor = Cursor<u8>;

} // namespace rstd::parse
