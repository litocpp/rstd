module;
#include <rstd/enum.hpp>

export module rstd.json:serialize;
export import :value;

export namespace rstd::json
{

struct FormatOptions {
    bool  pretty { false };
    usize indent { 2 };
};

auto to_string(const Value& value) -> ::alloc::string::String;
auto to_string(const Value& value, FormatOptions options) -> ::alloc::string::String;

} // namespace rstd::json

using namespace rstd::prelude;
using namespace rstd::json;
using namespace rstd::literals;

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
                    if (! write(rstd::from_utf8_unchecked(escape.as_slice()))) return false;
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
        for (usize i {}; i < array.len(); ++i) {
            if (options_.pretty && ! write_indent(depth + usize(1))) return false;
            if (! write_value(array[i], depth + usize(1))) return false;
            if (i + usize(1) != array.len() && ! write_byte(u8(','))) return false;
            if (options_.pretty && ! write_byte(u8('\n'))) return false;
        }
        if (options_.pretty && ! write_indent(depth)) return false;
        return write_byte(u8(']'));
    }

    auto write_object(const Map& object, usize depth) -> bool {
        if (! write_byte(u8('{'))) return false;
        if (object.is_empty()) return write_byte(u8('}'));

        if (options_.pretty && ! write_byte(u8('\n'))) return false;
        usize index {};
        auto  iter = object.iter();
        for (auto item = iter.next(); item.is_some(); item = iter.next(), ++index) {
            if (options_.pretty && ! write_indent(depth + usize(1))) return false;
            if (! write_string((*item).template get<0>()->as_str())) return false;
            if (! write(options_.pretty ? ": "_str : ":"_str)) return false;
            if (! write_value(*(*item).template get<1>(), depth + usize(1))) return false;
            if (index + usize(1) != object.len() && ! write_byte(u8(','))) return false;
            if (options_.pretty && ! write_byte(u8('\n'))) return false;
        }
        if (options_.pretty && ! write_indent(depth)) return false;
        return write_byte(u8('}'));
    }

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
