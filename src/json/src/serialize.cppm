module;
#include <rstd/enum.hpp>

export module rstd.json:serialize;
export import :value;
export import rstd.serde;

export namespace rstd::json
{

/// Options that control JSON text formatting.
struct FormatOptions {
    /// Writes indented multi-line JSON when enabled.
    bool pretty { false };
    /// Sets the number of spaces used for each indentation level.
    usize indent { 2 };
};

/// Serializes a JSON value to compact text.
auto to_string(const Value& value) -> ::alloc::string::String;
/// Serializes a JSON value using explicit formatting options.
auto to_string(const Value& value, FormatOptions options) -> ::alloc::string::String;

} // namespace rstd::json

using namespace rstd::prelude;
using namespace rstd::json;
using namespace rstd::literals;

class DirectSerializer;
class DirectSequenceSerializer;
class DirectMapSerializer;

class Emitter {
    rstd::fmt::Formatter& formatter_;
    FormatOptions         options_;

    auto write(ref<str> value) -> bool { return formatter_.write_str(value); }

    auto write_byte(u8 value) -> bool {
        auto const byte = value.to_primitive();
        return formatter_.write_raw(&byte, rstd::size_t(1));
    }

    auto write_indent(usize depth) -> bool {
        const usize count = depth * options_.indent;
        for (usize i {}; i < count; ++i) {
            if (! write_byte(u8(' '))) return false;
        }
        return true;
    }

    auto write_string(ref<str> value) -> bool {
        if (! write_byte(u8('"'))) return false;
        static constexpr auto HEX = "0123456789abcdef"_str;
        usize                 chunk_start {};
        auto                  flush_chunk = [&](usize end) {
            if (chunk_start == end) return true;
            return write(ref<str>::from_raw_parts_unchecked(
                value.data() + chunk_start.to_primitive(), end - chunk_start));
        };

        for (usize i {}; i < value.size(); ++i) {
            const u8 byte = value[i];
            ref<str> replacement;
            switch (byte.to_primitive()) {
            case '"': replacement = "\\\""_str; break;
            case '\\': replacement = "\\\\"_str; break;
            case '\b': replacement = "\\b"_str; break;
            case '\f': replacement = "\\f"_str; break;
            case '\n': replacement = "\\n"_str; break;
            case '\r': replacement = "\\r"_str; break;
            case '\t': replacement = "\\t"_str; break;
            default:
                if (byte < u8(0x20)) {
                    if (! flush_chunk(i)) return false;
                    auto escape =
                        rstd::array<u8, 6> { u8('\\'),
                                             u8('u'),
                                             u8('0'),
                                             u8('0'),
                                             HEX[usize((byte >> u64(4)).to_primitive())],
                                             HEX[usize((byte & u8(0x0f)).to_primitive())] };
                    if (! write(rstd::str_::from_utf8_unchecked(escape.as_slice()))) return false;
                    chunk_start = i + usize(1);
                }
                continue;
            }
            if (! flush_chunk(i) || ! write(replacement)) return false;
            chunk_start = i + usize(1);
        }
        return flush_chunk(value.size()) && write_byte(u8('"'));
    }

    template<typename T>
    auto write_integer(T value) -> bool {
        return formatter_.write_fmt(rstd::fmt::Arguments::make("{}", value));
    }

    auto write_float(f64 value) -> bool {
        struct Buffer {
            rstd::uint8_t bytes[64];
            rstd::size_t  len = 0;
        } buffer;
        rstd::fmt::Formatter local(
            &buffer, [](void* context, const rstd::uint8_t* bytes, rstd::size_t len) -> bool {
                auto& output = *static_cast<Buffer*>(context);
                if (output.len + len > sizeof(output.bytes)) return false;
                for (rstd::size_t i = 0; i < len; ++i) output.bytes[output.len++] = bytes[i];
                return true;
            });
        if (! local.write_fmt(rstd::fmt::Arguments::make("{:?}", value))) return false;

        rstd::size_t exponent = buffer.len;
        for (rstd::size_t i = 0; i < buffer.len; ++i) {
            if (buffer.bytes[i] == 'e') {
                exponent = i;
                break;
            }
        }
        if (exponent == buffer.len) return formatter_.write_raw(buffer.bytes, buffer.len);
        if (! formatter_.write_raw(buffer.bytes, exponent + 1)) return false;
        if (buffer.bytes[exponent + 1] != '-' && ! write_byte(u8('+'))) return false;
        return formatter_.write_raw(buffer.bytes + exponent + 1, buffer.len - exponent - 1);
    }

    auto write_number(const Number& number) -> bool {
        if (number.is_u64()) return write_integer(*number.as_u64());
        if (number.is_i64()) return write_integer(*number.as_i64());
        return write_float(*number.as_f64());
    }

    auto write_array(const Array& array, usize depth) -> bool {
        if (! write_byte(u8('['))) return false;
        if (array.is_empty()) return write_byte(u8(']'));

        if (options_.pretty && ! write_byte(u8('\n'))) return false;
        for (auto [index, value] : array.iter().enumerate()) {
            if (options_.pretty && ! write_indent(depth + usize(1))) return false;
            if (! write_value(*value, depth + usize(1))) return false;
            if (index + usize(1) != array.len() && ! write_byte(u8(','))) return false;
            if (options_.pretty && ! write_byte(u8('\n'))) return false;
        }
        if (options_.pretty && ! write_indent(depth)) return false;
        return write_byte(u8(']'));
    }

    auto write_object(const Map& object, usize depth) -> bool {
        if (! write_byte(u8('{'))) return false;
        if (object.is_empty()) return write_byte(u8('}'));

        if (options_.pretty && ! write_byte(u8('\n'))) return false;
        for (auto [index, item] : object.iter().enumerate()) {
            auto [key, value] = item;
            if (options_.pretty && ! write_indent(depth + usize(1))) return false;
            if (! write_string(key->as_str())) return false;
            if (! write(options_.pretty ? ": "_str : ":"_str)) return false;
            if (! write_value(*value, depth + usize(1))) return false;
            if (index + usize(1) != object.len() && ! write_byte(u8(','))) return false;
            if (options_.pretty && ! write_byte(u8('\n'))) return false;
        }
        if (options_.pretty && ! write_indent(depth)) return false;
        return write_byte(u8('}'));
    }

    friend class DirectSerializer;
    friend class DirectSequenceSerializer;
    friend class DirectMapSerializer;

public:
    Emitter(rstd::fmt::Formatter& formatter, FormatOptions options)
        : formatter_(formatter), options_(options) {}

    auto write_value(const Value& value, usize depth = usize()) -> bool {
        RSTD_MATCH(value) {
            RSTD_CASE(Null) {
                return write("null"_str);
            }
            RSTD_CASE(Bool, boolean) {
                return write(boolean ? "true"_str : "false"_str);
            }
            RSTD_CASE(Number, number) {
                return write_number(number);
            }
            RSTD_CASE(String, string) {
                return write_string(string.as_str());
            }
            RSTD_CASE(Array, array) {
                return write_array(array, depth);
            }
            RSTD_CASE(Object, object) {
                return write_object(object, depth);
            }
        }
        rstd::unreachable();
    }
};

class DirectSerializer {
    Emitter* emitter_ {};
    usize    depth_ {};

    auto written(bool success) const -> Result<empty, rstd::serde::Error> {
        if (success) return Ok(empty {});
        return Err(rstd::serde::Error::invariant({}, "failed to write JSON output"_str));
    }

    friend class DirectSequenceSerializer;
    friend class DirectMapSerializer;

public:
    using value_type    = empty;
    using error_type    = rstd::serde::Error;
    using result_type   = Result<value_type, error_type>;
    using sequence_type = DirectSequenceSerializer;
    using map_type      = DirectMapSerializer;

    explicit DirectSerializer(Emitter& emitter, usize depth = {})
        : emitter_(rstd::addressof(emitter)), depth_(depth) {}

    auto serialize_none() -> result_type { return written(emitter_->write("null"_str)); }
    auto serialize_unit() -> result_type { return serialize_none(); }
    auto serialize_bool(bool value) -> result_type {
        return written(emitter_->write(value ? "true"_str : "false"_str));
    }
    auto serialize_i64(i64 value) -> result_type { return written(emitter_->write_integer(value)); }
    auto serialize_u64(u64 value) -> result_type { return written(emitter_->write_integer(value)); }
    auto serialize_f64(f64 value) -> result_type { return written(emitter_->write_float(value)); }
    auto serialize_string(ref<str> value) -> result_type {
        return written(emitter_->write_string(value));
    }
    auto serialize_bytes(slice<u8> value) -> result_type;

    template<typename T>
    auto serialize_some(const T& value) -> result_type {
        return rstd::serde::serialize(*this, value);
    }

    template<typename T>
    auto serialize_newtype(ref<str>, const T& value) -> result_type {
        return rstd::serde::serialize(*this, value);
    }

    auto serialize_unit_variant(ref<str> variant) -> result_type {
        return serialize_string(variant);
    }

    template<typename T>
    auto serialize_newtype_variant(ref<str> variant, const T& value) -> result_type {
        if (! emitter_->write_byte(u8('{')) ||
            (emitter_->options_.pretty &&
             (! emitter_->write_byte(u8('\n')) || ! emitter_->write_indent(depth_ + usize(1)))) ||
            ! emitter_->write_string(variant) ||
            ! emitter_->write(emitter_->options_.pretty ? ": "_str : ":"_str)) {
            return written(false);
        }
        auto child  = DirectSerializer(*emitter_, depth_ + usize(1));
        auto result = rstd::serde::serialize(child, value);
        if (result.is_err()) return result;
        if (emitter_->options_.pretty &&
            (! emitter_->write_byte(u8('\n')) || ! emitter_->write_indent(depth_))) {
            return written(false);
        }
        return written(emitter_->write_byte(u8('}')));
    }

    auto begin_sequence(usize len) -> Result<DirectSequenceSerializer, error_type>;
    auto begin_map(usize len) -> Result<DirectMapSerializer, error_type>;

    auto invalid_value(ref<str> message) const -> error_type {
        return rstd::serde::Error::invalid_value({}, message);
    }
    auto unsupported(ref<str> message) const -> error_type {
        return rstd::serde::Error::unsupported({}, message);
    }

    template<typename T>
    auto serialize_extension(ref<str>, const T&) -> result_type {
        return Err(unsupported("JSON does not support this extension"_str));
    }
};

class DirectSequenceSerializer {
    Emitter* emitter_ {};
    usize    depth_ {};
    usize    expected_ {};
    usize    written_ {};
    bool     ended_ {};

public:
    using error_type = rstd::serde::Error;

    DirectSequenceSerializer(Emitter& emitter, usize expected, usize depth)
        : emitter_(rstd::addressof(emitter)), depth_(depth), expected_(expected) {}

    template<typename T>
    auto element(const T& value) -> Result<empty, error_type> {
        if (ended_) return Err(error_type::invariant({}, "sequence already ended"_str));
        if (written_ != usize {} && ! emitter_->write_byte(u8(','))) {
            return Err(error_type::invariant({}, "failed to write JSON output"_str));
        }
        if (emitter_->options_.pretty &&
            ((written_ != usize {} && ! emitter_->write_byte(u8('\n'))) ||
             ! emitter_->write_indent(depth_ + usize(1)))) {
            return Err(error_type::invariant({}, "failed to write JSON output"_str));
        }
        auto serializer = DirectSerializer(*emitter_, depth_ + usize(1));
        auto result     = rstd::serde::serialize(serializer, value);
        if (result.is_err()) return result;
        ++written_;
        return Ok(empty {});
    }

    auto end() -> Result<empty, error_type> {
        if (ended_) return Err(error_type::invariant({}, "sequence already ended"_str));
        if (written_ != expected_) {
            return Err(error_type::invariant({}, "sequence length does not match"_str));
        }
        ended_ = true;
        if (emitter_->options_.pretty && written_ != usize {} &&
            (! emitter_->write_byte(u8('\n')) || ! emitter_->write_indent(depth_))) {
            return Err(error_type::invariant({}, "failed to write JSON output"_str));
        }
        if (! emitter_->write_byte(u8(']'))) {
            return Err(error_type::invariant({}, "failed to write JSON output"_str));
        }
        return Ok(empty {});
    }
};

class DirectMapSerializer {
    Emitter* emitter_ {};
    usize    depth_ {};
    usize    expected_ {};
    usize    written_ {};
    bool     pending_ {};
    bool     ended_ {};

public:
    using error_type = rstd::serde::Error;

    DirectMapSerializer(Emitter& emitter, usize expected, usize depth)
        : emitter_(rstd::addressof(emitter)), depth_(depth), expected_(expected) {}

    template<typename T>
    auto key(const T& value) -> Result<empty, error_type> {
        if (ended_) return Err(error_type::invariant({}, "map already ended"_str));
        if (pending_) return Err(error_type::invariant({}, "map value is missing"_str));
        if (written_ != usize {} && ! emitter_->write_byte(u8(','))) {
            return Err(error_type::invariant({}, "failed to write JSON output"_str));
        }
        if (emitter_->options_.pretty &&
            ((written_ != usize {} && ! emitter_->write_byte(u8('\n'))) ||
             ! emitter_->write_indent(depth_ + usize(1)))) {
            return Err(error_type::invariant({}, "failed to write JSON output"_str));
        }
        bool success = false;
        if constexpr (rstd::mtp::same_as<rstd::mtp::rm_cvf<T>, ::alloc::string::String>) {
            success = emitter_->write_string(value.as_str());
        } else if constexpr (rstd::mtp::same_as<rstd::mtp::rm_cvf<T>, ref<str>>) {
            success = emitter_->write_string(value);
        } else {
            return Err(error_type::unsupported({}, "JSON object key must be a string"_str));
        }
        if (! success || ! emitter_->write(emitter_->options_.pretty ? ": "_str : ":"_str)) {
            return Err(error_type::invariant({}, "failed to write JSON output"_str));
        }
        pending_ = true;
        return Ok(empty {});
    }

    template<typename T>
    auto value(const T& value) -> Result<empty, error_type> {
        if (ended_) return Err(error_type::invariant({}, "map already ended"_str));
        if (! pending_) return Err(error_type::invariant({}, "map key is missing"_str));
        auto serializer = DirectSerializer(*emitter_, depth_ + usize(1));
        auto result     = rstd::serde::serialize(serializer, value);
        if (result.is_err()) return result;
        pending_ = false;
        ++written_;
        return Ok(empty {});
    }

    auto end() -> Result<empty, error_type> {
        if (ended_) return Err(error_type::invariant({}, "map already ended"_str));
        if (pending_) return Err(error_type::invariant({}, "map value is missing"_str));
        if (written_ != expected_) {
            return Err(error_type::invariant({}, "map length does not match"_str));
        }
        ended_ = true;
        if (emitter_->options_.pretty && written_ != usize {} &&
            (! emitter_->write_byte(u8('\n')) || ! emitter_->write_indent(depth_))) {
            return Err(error_type::invariant({}, "failed to write JSON output"_str));
        }
        if (! emitter_->write_byte(u8('}'))) {
            return Err(error_type::invariant({}, "failed to write JSON output"_str));
        }
        return Ok(empty {});
    }
};

inline auto DirectSerializer::begin_sequence(usize len)
    -> Result<DirectSequenceSerializer, error_type> {
    if (! emitter_->write_byte(u8('[')) ||
        (emitter_->options_.pretty && len != usize {} && ! emitter_->write_byte(u8('\n')))) {
        return Err(error_type::invariant({}, "failed to write JSON output"_str));
    }
    return Ok(DirectSequenceSerializer(*emitter_, len, depth_));
}

inline auto DirectSerializer::begin_map(usize len) -> Result<DirectMapSerializer, error_type> {
    if (! emitter_->write_byte(u8('{')) ||
        (emitter_->options_.pretty && len != usize {} && ! emitter_->write_byte(u8('\n')))) {
        return Err(error_type::invariant({}, "failed to write JSON output"_str));
    }
    return Ok(DirectMapSerializer(*emitter_, len, depth_));
}

inline auto DirectSerializer::serialize_bytes(slice<u8> value) -> result_type {
    auto sequence = begin_sequence(value.len());
    if (sequence.is_err()) return Err(rstd::move(sequence).unwrap_err_unchecked());
    auto output = rstd::move(sequence).unwrap_unchecked();
    for (auto byte : value) {
        auto result = output.element(byte);
        if (result.is_err()) return result;
    }
    return output.end();
}

static_assert(rstd::serde::Serializer<DirectSerializer>);

namespace rstd::json
{

auto to_string(const Value& value) -> ::alloc::string::String {
    return to_string(value, FormatOptions {});
}

auto to_string(const Value& value, FormatOptions options) -> ::alloc::string::String {
    auto           output = ::alloc::string::String::make();
    fmt::Formatter formatter(output);
    Emitter        emitter(formatter, options);
    if (! emitter.write_value(value)) rstd::panic { "failed to serialize JSON value" };
    return output;
}

export template<typename T>
    requires rstd::serde::Serializable<T>
auto encode_direct(const T& value, FormatOptions options = {})
    -> Result<::alloc::string::String, rstd::serde::Error> {
    auto           output = ::alloc::string::String::make();
    fmt::Formatter formatter(output);
    Emitter        emitter(formatter, options);
    auto           serializer = DirectSerializer(emitter);
    auto           result     = rstd::serde::serialize(serializer, value);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    return Ok(rstd::move(output));
}

} // namespace rstd::json

namespace rstd
{

template<>
struct Impl<fmt::Display, json::Value> : ImplBase<json::Value> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        Emitter emitter(
            formatter, json::FormatOptions { .pretty = formatter.alternate(), .indent = usize(2) });
        return emitter.write_value(this->self());
    }
};

} // namespace rstd
