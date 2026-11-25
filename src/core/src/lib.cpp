module;
#include <rstd/runtime.h>
#include <string_view>

module rstd.core;
import :core;
import :fmt;
import :fmt.integer;
import :num.types;
import :panicking;
import :str.str;

using rstd::panic_::PanicInfo;

namespace
{
[[noreturn]]
void runtime_panic(PanicInfo const& info) {
    const auto runtime_info = rstd_panic_info {
        .data               = info.data,
        .format             = info.fmt,
        .file               = info.location.file_name(),
        .function           = info.location.function_name(),
        .line               = info.location.line(),
        .column             = info.location.column(),
        .can_unwind         = info.can_unwind,
        .force_no_backtrace = info.force_no_backtrace,
    };
    [[unsafe]] { rstd_panic_impl(&runtime_info); }
}

bool panic_write_fmt(void const* data, void* ctx, rstd::panic_::WriteFn write) {
    rstd::fmt::Formatter f(ctx, write);
    [[unsafe]] { return f.write_fmt(*static_cast<rstd::fmt::Arguments const*>(data)); }
}
} // namespace

namespace rstd
{

void panic_fmt(fmt::Arguments args, source_location loc) {
    auto info = PanicInfo {
        .data     = &args,
        .fmt      = panic_write_fmt,
        .location = panic_::Location::from(loc),
    };
    runtime_panic(info);
}

void panic_fmt_nounwind(fmt::Arguments args, source_location loc) {
    auto info = PanicInfo {
        .data       = &args,
        .fmt        = panic_write_fmt,
        .location   = panic_::Location::from(loc),
        .can_unwind = false,
    };
    runtime_panic(info);
}

void panic_message(ref<str> message, source_location loc) {
    fmt::Argument         argument = fmt::Argument::make(message);
    static constexpr char format[] = "{}";
    panic_fmt({ format, rstd::size_t(2), &argument, rstd::size_t(1) }, loc);
}

} // namespace rstd

namespace rstd::ptr_detail
{

void panic_empty_ref(source_location loc) noexcept {
    constexpr char message[] = "dereference of an empty reference";
    panic_fmt_nounwind({ message, sizeof(message) - 1, nullptr, 0 }, loc);
}

void panic_slice_index(source_location loc) noexcept {
    constexpr char message[] = "slice index out of bounds";
    panic_fmt_nounwind({ message, sizeof(message) - 1, nullptr, 0 }, loc);
}

} // namespace rstd::ptr_detail

[[noreturn]]
void panic_overflow(rstd::source_location loc) {
    rstd::panic_message("attempt to perform integer arithmetic with overflow", loc);
}

[[noreturn]]
void panic_divide_by_zero(rstd::source_location loc) {
    rstd::panic_message("attempt to divide by zero", loc);
}

[[noreturn]]
void panic_invalid_shift(rstd::source_location loc) {
    rstd::panic_message("attempt to shift with overflow", loc);
}

[[noreturn]]
void panic_invalid_float_clamp(rstd::source_location loc) {
    rstd::panic_message("min > max, or either was NaN", loc);
}

namespace rstd::fmt
{

auto Formatter::write_str(ref<str> const& value) -> bool {
    [[unsafe]] {
        if (_write_func != nullptr) return _write_func(_writer, value);
        return _raw_write_func(_writer,
                               reinterpret_cast<rstd::uint8_t const*>(value.data()),
                               value.size().to_primitive());
    }
}

auto Formatter::write_raw(const rstd::byte* data, rstd::size_t length) -> bool {
    [[unsafe]] {
        if (! char_::is_valid_utf8(data, usize(length))) return false;
        auto bytes = slice<u8>::from_raw_parts(data, usize(length));
        return write_str(rstd::str_::from_utf8_unchecked(bytes));
    }
}

auto Formatter::write_raw(const rstd::uint8_t* data, rstd::size_t length) -> bool {
    [[unsafe]] { return write_raw(reinterpret_cast<rstd::byte const*>(data), length); }
}

auto Formatter::write_raw(const char* data, rstd::size_t length) -> bool {
    [[unsafe]] { return write_raw(reinterpret_cast<rstd::byte const*>(data), length); }
}

// ── Spec parser ───────────────────────────────────────────────────────────
// Parses the content between '{' ... '}' after any arg-id and ':'.
// Syntax: [[fill]align][sign][#][0][width][.precision][type]
//   fill      = any char (default ' ')
//   align     = '<' | '^' | '>'
//   sign      = '+' | '-'
//   alternate = '#'
//   zero_pad  = '0'
//   width     = [1-9][0-9]*
//   precision = '.' [0-9]+
//   type      = '?' | 'b' | 'd' | 'o' | 'x' | 'X' | 'e' | 'E' | 'p' | 's'
namespace
{
auto format_part(std::string_view text, rstd::size_t start, rstd::size_t count)
    -> std::string_view {
    if (count == 0) return {};
    [[unsafe]] { return { text.data() + start, count }; }
}

auto parse_spec(std::string_view spec) -> FormattingOptions {
    using Opts = FormattingOptions;
    Opts opts {};
    if (spec.empty()) return opts;

    auto align_of = [](char c) -> Align {
        if (c == '<') return Align::Left;
        if (c == '>') return Align::Right;
        if (c == '^') return Align::Center;
        return Align::None;
    };

    // [[fill]align]
    if (spec.size() >= 2 && align_of(spec[1]) != Align::None) {
        opts.set_fill(spec[0]).set_align(align_of(spec[1]));
        spec.remove_prefix(2);
    } else if (align_of(spec[0]) != Align::None) {
        opts.set_align(align_of(spec[0]));
        spec.remove_prefix(1);
    }

    // sign
    if (! spec.empty() && spec[0] == '+') {
        opts.set_flag(Opts::SIGN_PLUS);
        spec.remove_prefix(1);
    } else if (! spec.empty() && spec[0] == '-') {
        opts.set_flag(Opts::SIGN_MINUS);
        spec.remove_prefix(1);
    }

    // alternate
    if (! spec.empty() && spec[0] == '#') {
        opts.set_flag(Opts::ALTERNATE);
        spec.remove_prefix(1);
    }

    // zero-pad
    if (! spec.empty() && spec[0] == '0') {
        opts.set_flag(Opts::ZERO_PAD);
        spec.remove_prefix(1);
    }

    // width: [1-9][0-9]*
    if (! spec.empty() && spec[0] >= '1' && spec[0] <= '9') {
        rstd::uint16_t w = 0;
        while (! spec.empty() && static_cast<unsigned char>(spec[0] - '0') < 10u) {
            w = static_cast<rstd::uint16_t>(w * 10 + (spec[0] - '0'));
            spec.remove_prefix(1);
        }
        opts.set_width(w);
    }

    // .precision
    if (! spec.empty() && spec[0] == '.') {
        spec.remove_prefix(1);
        rstd::uint16_t p = 0;
        while (! spec.empty() && static_cast<unsigned char>(spec[0] - '0') < 10u) {
            p = static_cast<rstd::uint16_t>(p * 10 + (spec[0] - '0'));
            spec.remove_prefix(1);
        }
        opts.set_precision(p);
    }

    // type char
    if (! spec.empty()) {
        switch (spec[0]) {
        case '?': opts.set_presentation(Presentation::Debug); break;
        case 'e': opts.set_presentation(Presentation::LowerExp); break;
        case 'E': opts.set_presentation(Presentation::UpperExp); break;
        case 'x': opts.set_presentation(Presentation::LowerHex); break;
        case 'X': opts.set_presentation(Presentation::UpperHex); break;
        case 'o': opts.set_presentation(Presentation::Octal); break;
        default: break;
        }
    }

    return opts;
}
} // anonymous namespace

auto Formatter::pad_numeric(const rstd::uint8_t* sign,
                            rstd::size_t         sign_len,
                            const rstd::uint8_t* significand,
                            rstd::size_t         significand_len,
                            rstd::size_t         zero_count,
                            const rstd::uint8_t* exponent,
                            rstd::size_t         exponent_len) -> bool {
    auto write_repeat = [this](rstd::uint8_t value, rstd::size_t count) -> bool {
        rstd::uint8_t buffer[64];
        for (auto& item : buffer) item = value;
        while (count != 0) {
            auto chunk = count < sizeof(buffer) ? count : sizeof(buffer);
            if (! write_raw(buffer, chunk)) return false;
            count -= chunk;
        }
        return true;
    };

    auto write_number = [&]() -> bool {
        if (sign_len != 0 && ! write_raw(sign, sign_len)) return false;
        if (significand_len != 0 && ! write_raw(significand, significand_len)) return false;
        if (! write_repeat('0', zero_count)) return false;
        return exponent_len == 0 || write_raw(exponent, exponent_len);
    };

    const rstd::size_t length = sign_len + significand_len + zero_count + exponent_len;
    const rstd::size_t width  = has_width() ? this->width() : 0;
    if (length >= width) return write_number();

    const rstd::size_t padding = width - length;
    if (zero_pad()) {
        if (sign_len != 0 && ! write_raw(sign, sign_len)) return false;
        if (! write_repeat('0', padding)) return false;
        if (significand_len != 0 && ! write_raw(significand, significand_len)) return false;
        if (! write_repeat('0', zero_count)) return false;
        return exponent_len == 0 || write_raw(exponent, exponent_len);
    }

    auto alignment = align();
    if (alignment == Align::None) alignment = Align::Right;
    rstd::size_t left = padding;
    if (alignment == Align::Left) left = 0;
    if (alignment == Align::Center) left = padding / 2;
    const rstd::size_t right = padding - left;

    if (! write_repeat(static_cast<rstd::uint8_t>(fill()), left)) return false;
    if (! write_number()) return false;
    return write_repeat(static_cast<rstd::uint8_t>(fill()), right);
}

auto Formatter::pad(ref<str> value) -> bool {
    if (! has_width() && ! has_prec()) return write_raw(value.data(), value.size().to_primitive());

    const rstd::size_t maximum =
        has_prec() ? static_cast<rstd::size_t>(precision()) : value.size().to_primitive();
    auto         iterator = value.chars();
    rstd::size_t chars    = 0;
    while (chars < maximum && iterator.next().is_some()) ++chars;
    const rstd::size_t bytes =
        value.size().to_primitive() - iterator.as_str().size().to_primitive();

    const rstd::size_t target_width = has_width() ? static_cast<rstd::size_t>(width()) : 0;
    if (chars >= target_width) return write_raw(value.data(), bytes);

    const rstd::size_t padding   = target_width - chars;
    auto               alignment = align();
    if (alignment == Align::None) alignment = Align::Left;

    rstd::size_t left = 0;
    if (alignment == Align::Right) left = padding;
    if (alignment == Align::Center) left = padding / 2;
    const rstd::size_t right = padding - left;

    auto write_fill = [this](rstd::size_t count) -> bool {
        rstd::uint8_t buffer[64];
        for (auto& item : buffer) item = static_cast<rstd::uint8_t>(fill());
        while (count != 0) {
            const rstd::size_t chunk = count < sizeof(buffer) ? count : sizeof(buffer);
            if (! write_raw(buffer, chunk)) return false;
            count -= chunk;
        }
        return true;
    };

    if (! write_fill(left)) return false;
    if (! write_raw(value.data(), bytes)) return false;
    return write_fill(right);
}

// ── Formatter::write_fmt ──────────────────────────────────────────────────
auto Formatter::write_fmt(Arguments args) -> bool {
    const std::string_view text(args.fmt_ptr, args.fmt_len);
    rstd::size_t           arg_idx       = 0;
    rstd::size_t           pos           = 0;
    rstd::size_t           last          = 0;
    auto                   write_literal = [&](rstd::size_t end) {
        auto literal = format_part(text, last, end - last);
        return literal.empty() || write_raw(literal.data(), literal.size());
    };

    while (pos < text.size()) {
        if (text[pos] == '{') {
            if (pos + 1 < text.size() && text[pos + 1] == '{') {
                // Escaped {{
                if (! write_literal(pos)) return false;
                constexpr char LEFT_BRACE[] = { '{' };
                if (! write_raw(LEFT_BRACE, sizeof(LEFT_BRACE))) return false;
                pos += 2;
                last = pos;
                continue;
            }
            // Flush literal text before placeholder.
            if (! write_literal(pos)) return false;
            ++pos; // skip '{'

            // Scan to matching '}'.
            auto begin = pos;
            auto end   = begin;
            while (end < text.size() && text[end] != '}') ++end;
            if (end == text.size()) return false; // unmatched '{'
            auto field = format_part(text, begin, end - begin);
            pos        = end + 1;
            last       = pos;

            auto argument = parse_format_argument(field, arg_idx, args.args_len);
            if (! argument.valid) return false;

            // Parse spec, set options, dispatch, then restore options
            // (so nested write_fmt calls don't see stale options).
            auto saved = Formatter_set_options(
                *this, parse_spec(format_part(field, argument.spec, field.size() - argument.spec)));
            bool ok;
            [[unsafe]] { ok = args.args_ptr[argument.index].fmt(*this); }
            Formatter_restore_options(*this, saved);
            if (! ok) return false;
            if (argument.automatic) ++arg_idx;

        } else if (text[pos] == '}') {
            if (pos + 1 < text.size() && text[pos + 1] == '}') {
                // Escaped }}
                if (! write_literal(pos)) return false;
                constexpr char RIGHT_BRACE[] = { '}' };
                if (! write_raw(RIGHT_BRACE, sizeof(RIGHT_BRACE))) return false;
                pos += 2;
                last = pos;
                continue;
            }
            return false; // unmatched '}'
        } else {
            ++pos;
        }
    }

    return write_literal(text.size());
}

} // namespace rstd::fmt
