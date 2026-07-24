module rstd.core;
import :core;
import :fmt;
import :num.types;
import :panicking;
import :str.str;

using rstd::panic_::PanicInfo;

extern "C" [[noreturn]]
void rstd_panic_impl(PanicInfo const& info);

namespace
{
bool panic_write_fmt(void const* data, void* ctx, rstd::panic_::WriteFn write) {
    auto const&          args = *static_cast<rstd::fmt::Arguments const*>(data);
    rstd::fmt::Formatter f(ctx, write);
    return f.write_fmt(args);
}
} // namespace

namespace rstd
{

void panic_fmt(fmt::Arguments args, panic_::Location loc) {
    auto info = PanicInfo {
        .data     = &args,
        .fmt      = panic_write_fmt,
        .location = loc,
    };
    rstd_panic_impl(info);
}

void panic_fmt_nounwind(fmt::Arguments args, panic_::Location loc) {
    auto info = PanicInfo {
        .data       = &args,
        .fmt        = panic_write_fmt,
        .location   = loc,
        .can_unwind = false,
    };
    rstd_panic_impl(info);
}

} // namespace rstd

[[noreturn]]
void panic_overflow() {
    rstd::panic { "attempt to perform integer arithmetic with overflow" };
}

[[noreturn]]
void panic_divide_by_zero() {
    rstd::panic { "attempt to divide by zero" };
}

[[noreturn]]
void panic_invalid_shift() {
    rstd::panic { "attempt to shift with overflow" };
}

[[noreturn]]
void panic_invalid_float_clamp() {
    rstd::panic { "min > max, or either was NaN" };
}

namespace rstd::fmt
{

auto Formatter::write_str(ref<str> const& value) -> bool {
    if (_write_func != nullptr) return _write_func(_writer, value);
    return _raw_write_func(
        _writer, reinterpret_cast<rstd::uint8_t const*>(value.data()), value.size().to_primitive());
}

auto Formatter::write_raw(const rstd::byte* data, rstd::size_t length) -> bool {
    auto bytes = slice<u8>::from_raw_parts(data, usize(length));
    if (! char_::is_valid_utf8(data, usize(length))) return false;
    return write_str(rstd::from_utf8_unchecked(bytes));
}

auto Formatter::write_raw(const rstd::uint8_t* data, rstd::size_t length) -> bool {
    return write_raw(reinterpret_cast<rstd::byte const*>(data), length);
}

auto Formatter::write_raw(const char* data, rstd::size_t length) -> bool {
    return write_raw(reinterpret_cast<rstd::byte const*>(data), length);
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
auto parse_spec(const char* b, const char* e) -> FormattingOptions {
    using Opts = FormattingOptions;
    Opts opts {};
    if (b >= e) return opts;

    auto align_of = [](char c) -> Align {
        if (c == '<') return Align::Left;
        if (c == '>') return Align::Right;
        if (c == '^') return Align::Center;
        return Align::None;
    };

    // [[fill]align]
    if (b + 1 < e && align_of(b[1]) != Align::None) {
        opts.set_fill(b[0]).set_align(align_of(b[1]));
        b += 2;
    } else if (b < e && align_of(b[0]) != Align::None) {
        opts.set_align(align_of(b[0]));
        b++;
    }

    // sign
    if (b < e && b[0] == '+') {
        opts.set_flag(Opts::SIGN_PLUS);
        b++;
    } else if (b < e && b[0] == '-') {
        opts.set_flag(Opts::SIGN_MINUS);
        b++;
    }

    // alternate
    if (b < e && b[0] == '#') {
        opts.set_flag(Opts::ALTERNATE);
        b++;
    }

    // zero-pad
    if (b < e && b[0] == '0') {
        opts.set_flag(Opts::ZERO_PAD);
        b++;
    }

    // width: [1-9][0-9]*
    if (b < e && b[0] >= '1' && b[0] <= '9') {
        rstd::uint16_t w = 0;
        while (b < e && (unsigned char)(b[0] - '0') < 10u) {
            w = static_cast<rstd::uint16_t>(w * 10 + (b[0] - '0'));
            b++;
        }
        opts.set_width(w);
    }

    // .precision
    if (b < e && b[0] == '.') {
        b++;
        rstd::uint16_t p = 0;
        while (b < e && (unsigned char)(b[0] - '0') < 10u) {
            p = static_cast<rstd::uint16_t>(p * 10 + (b[0] - '0'));
            b++;
        }
        opts.set_precision(p);
    }

    // type char
    if (b < e) {
        switch (b[0]) {
        case '?': opts.set_presentation(Presentation::Debug); break;
        case 'e': opts.set_presentation(Presentation::LowerExp); break;
        case 'E': opts.set_presentation(Presentation::UpperExp); break;
        // 'b' 'd' 'o' 'x' 'X' 'p' 's' — reserved for P2
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
        for (rstd::size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = value;
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
    auto         iterator = str_::chars(value);
    rstd::size_t chars    = 0;
    while (! iterator.is_empty() && chars < maximum) {
        iterator.next_unchecked();
        ++chars;
    }
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
        for (rstd::size_t i = 0; i < sizeof(buffer); ++i)
            buffer[i] = static_cast<rstd::uint8_t>(fill());
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
    rstd::size_t arg_idx = 0;
    const char*  p       = args.fmt_ptr;
    const char*  end     = args.fmt_ptr + args.fmt_len;
    const char*  last    = p;

    while (p < end) {
        if (*p == '{') {
            if (p + 1 < end && *(p + 1) == '{') {
                // Escaped {{
                if (p > last && ! write_raw(last, p - last)) return false;
                constexpr char LEFT_BRACE[] = { '{' };
                if (! write_raw(LEFT_BRACE, sizeof(LEFT_BRACE))) return false;
                p += 2;
                last = p;
                continue;
            }
            // Flush literal text before placeholder.
            if (p > last && ! write_raw(last, p - last)) return false;
            p++; // skip '{'

            // Scan to matching '}'.
            const char* inner = p;
            while (p < end && *p != '}') p++;
            if (p >= end) return false; // unmatched '{'
            const char* inner_end = p;
            p++;
            last = p;

            // Skip optional arg-id (digits), then optional ':'.
            const char* spec_b = inner;
            while (spec_b < inner_end && (unsigned char)(*spec_b - '0') < 10u) ++spec_b;
            if (spec_b < inner_end && *spec_b == ':') ++spec_b;
            const char* spec_e = inner_end;

            if (arg_idx >= args.args_len) return false;

            // Parse spec, set options, dispatch, then restore options
            // (so nested write_fmt calls don't see stale options).
            auto saved = Formatter_set_options(*this, parse_spec(spec_b, spec_e));
            bool ok    = args.args_ptr[arg_idx].fmt(*this);
            Formatter_restore_options(*this, saved);
            if (! ok) return false;
            arg_idx++;

        } else if (*p == '}') {
            if (p + 1 < end && *(p + 1) == '}') {
                // Escaped }}
                if (p > last && ! write_raw(last, p - last)) return false;
                constexpr char RIGHT_BRACE[] = { '}' };
                if (! write_raw(RIGHT_BRACE, sizeof(RIGHT_BRACE))) return false;
                p += 2;
                last = p;
                continue;
            }
            return false; // unmatched '}'
        } else {
            p++;
        }
    }

    if (p > last && ! write_raw(last, p - last)) return false;
    return true;
}

} // namespace rstd::fmt
