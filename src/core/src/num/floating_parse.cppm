export module rstd.core:num.floating_parse;
import :num.types;
import :error.trait;
import :ascii;
export import :num.dec2flt;
export import :str.traits;
export import :fmt;

export namespace rstd::num
{

enum class FloatErrorKind : rstd::uint8_t
{
    Empty,
    Invalid,
    PosOverflow,
    NegOverflow
};

class ParseFloatError {
    FloatErrorKind kind_;

public:
    explicit constexpr ParseFloatError(FloatErrorKind kind) noexcept: kind_(kind) {}
    [[nodiscard]]
    constexpr auto kind() const noexcept -> FloatErrorKind {
        return kind_;
    }
};

} // namespace rstd::num

namespace rstd
{

using namespace literals;

template<num::Float T>
struct Impl<str_::FromStr, T> {
    using Err  = num::ParseFloatError;
    using Self = T;

    static auto error(num::FloatErrorKind kind) -> Result<Self, Err> {
        return rstd::Err(num::ParseFloatError(kind));
    }

    static auto finish(f64 value, bool negative) -> Result<Self, Err> {
        if constexpr (mtp::same_as<Self, f64>) {
            return Ok(value);
        } else {
            auto primitive = static_cast<float>(value.to_primitive());
            if (__builtin_isinf(primitive) && value.is_finite()) {
                return error(negative ? num::FloatErrorKind::NegOverflow
                                      : num::FloatErrorKind::PosOverflow);
            }
            return Ok(f32(primitive));
        }
    }

    static auto from_str(ref<str> input) -> Result<Self, Err> {
        if (input.size() == usize()) return error(num::FloatErrorKind::Empty);

        auto byte_at = [input](rstd::size_t index) constexpr -> rstd::uint8_t {
            return input[usize(index)].to_primitive();
        };

        rstd::size_t index    = 0;
        bool         negative = false;
        if (byte_at(0) == '+') {
            index = 1;
        } else if (byte_at(0) == '-') {
            negative = true;
            index    = 1;
        }
        if (index == input.size().to_primitive()) {
            return error(num::FloatErrorKind::Invalid);
        }

        auto body = ref<str>::from_raw_parts_unchecked(input.data() + index,
                                                       usize(input.size().to_primitive() - index));
        if (body == "inf"_str || body == "infinity"_str) {
            return Ok(negative ? -Self::INFINITY_ : Self::INFINITY_);
        }
        if (body == "NaN"_str) return Ok(negative ? -Self::NAN_ : Self::NAN_);

        auto integer_begin = index;
        while (index < input.size().to_primitive() && ascii::is_digit(u8(byte_at(index)))) ++index;
        auto integer_end = index;

        auto fraction_begin = index;
        auto fraction_end   = index;
        if (index < input.size().to_primitive() && byte_at(index) == '.') {
            ++index;
            fraction_begin = index;
            while (index < input.size().to_primitive() && ascii::is_digit(u8(byte_at(index)))) {
                ++index;
            }
            fraction_end = index;
        }

        if (integer_begin == integer_end && fraction_begin == fraction_end) {
            return error(num::FloatErrorKind::Invalid);
        }

        i32 exponent {};
        if (index < input.size().to_primitive() &&
            (byte_at(index) == 'e' || byte_at(index) == 'E')) {
            ++index;
            bool exponent_negative = false;
            if (index < input.size().to_primitive() &&
                (byte_at(index) == '+' || byte_at(index) == '-')) {
                exponent_negative = byte_at(index) == '-';
                ++index;
            }
            if (index == input.size().to_primitive() || ! ascii::is_digit(u8(byte_at(index)))) {
                return error(num::FloatErrorKind::Invalid);
            }
            rstd::int32_t raw_exponent = 0;
            while (index < input.size().to_primitive() && ascii::is_digit(u8(byte_at(index)))) {
                if (raw_exponent < 100000) {
                    raw_exponent =
                        raw_exponent * 10 + static_cast<rstd::int32_t>(byte_at(index) - '0');
                }
                ++index;
            }
            exponent = i32(exponent_negative ? -raw_exponent : raw_exponent);
        }
        if (index != input.size().to_primitive()) {
            return error(num::FloatErrorKind::Invalid);
        }

        static constexpr byte ZERO[] = { byte { '0' } };
        auto integer  = integer_begin == integer_end
                            ? slice<u8>::from_raw_parts(ZERO, usize(1))
                            : slice<u8>::from_raw_parts(input.data() + integer_begin,
                                                        usize(integer_end - integer_begin));
        auto fraction = slice<u8>::from_raw_parts(input.data() + fraction_begin,
                                                  usize(fraction_end - fraction_begin));
        auto parsed   = num::dec2flt::to_f64({
            .integer  = integer,
            .fraction = fraction,
            .exponent = exponent,
            .negative = negative,
        });
        if (parsed.is_err()) {
            return error(negative ? num::FloatErrorKind::NegOverflow
                                  : num::FloatErrorKind::PosOverflow);
        }
        return finish(parsed.unwrap(), negative);
    }
};

template<>
struct Impl<fmt::Display, num::ParseFloatError> : ImplBase<num::ParseFloatError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const char* message = "invalid float literal";
        switch (this->self().kind()) {
        case num::FloatErrorKind::Empty: message = "cannot parse float from empty string"; break;
        case num::FloatErrorKind::Invalid: break;
        case num::FloatErrorKind::PosOverflow: message = "float literal is too large"; break;
        case num::FloatErrorKind::NegOverflow: message = "float literal is too small"; break;
        }
        return formatter.write_raw(message, rstd::strlen(message));
    }
};

template<>
struct Impl<fmt::Debug, num::ParseFloatError> : ImplBase<num::ParseFloatError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const char* kind = "Invalid";
        switch (this->self().kind()) {
        case num::FloatErrorKind::Empty: kind = "Empty"; break;
        case num::FloatErrorKind::Invalid: break;
        case num::FloatErrorKind::PosOverflow: kind = "PosOverflow"; break;
        case num::FloatErrorKind::NegOverflow: kind = "NegOverflow"; break;
        }
        constexpr char prefix[] = "ParseFloatError { kind: ";
        if (! formatter.write_raw(prefix, sizeof(prefix) - 1)) return false;
        if (! formatter.write_raw(kind, rstd::strlen(kind))) return false;
        return formatter.write_raw(" }", 2);
    }
};

template<>
struct Impl<error::Error, num::ParseFloatError> : ImplBase<num::ParseFloatError> {
    auto source() const noexcept -> Option<error::ErrorRef> { return None(); }
};

} // namespace rstd
