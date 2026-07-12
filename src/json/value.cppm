module;
#include <rstd/enum.hpp>

export module rstd.json:value;
export import :number;
export import rstd.alloc;

namespace rstd::json
{

export class Value;
export auto operator==(const Value& left, const Value& right) noexcept -> bool;

export template<mtp::is_int T>
    requires(! mtp::same_as<mtp::rm_cv<T>, bool>) && (sizeof(T) <= sizeof(u64))
auto operator==(const Value& left, T right) noexcept -> bool;

export auto operator==(const Value& left, bool right) noexcept -> bool;
export auto operator==(const Value& left, f32 right) noexcept -> bool;
export auto operator==(const Value& left, f64 right) noexcept -> bool;
export auto operator==(const Value& left, ref<str> right) noexcept -> bool;
export auto operator==(const Value& left, const ::alloc::string::String& right) noexcept -> bool;
export using Array = ::alloc::vec::Vec<Value>;
export using Map   = ::alloc::collections::BTreeMap<::alloc::string::String, Value>;

#define RSTD_JSON_VALUE_VARIANTS(V)             \
    V(Null, ())                                 \
    V(Bool, (bool value;))                      \
    V(Number, (rstd::json::Number value;))      \
    V(String, (::alloc::string::String value;)) \
    V(Array, (rstd::json::Array value;))        \
    V(Object, (rstd::json::Map value;))

export class Value : public rstd::DefaultInClass<Value, rstd::clone::Clone> {
    RSTD_ENUM_BODY_WITH_DEFAULT(Value, RSTD_JSON_VALUE_VARIANTS, Null)

private:
    static auto null_sentinel() noexcept -> const Value& {
        static const Value null;
        return null;
    }

    static auto parse_array_index(ref<str> token) noexcept -> Option<usize> {
        if (token.size() == 0 || token.data()[0] == '+' ||
            (token.size() > 1 && token.data()[0] == '0')) {
            return None();
        }

        usize value = 0;
        for (usize i = 0; i < token.size(); ++i) {
            const u8 byte = token.data()[i];
            if (byte < '0' || byte > '9') return None();
            const usize digit = byte - '0';
            if (value > (usize_::MAX - digit) / 10) return None();
            value = value * 10 + digit;
        }
        return Some(rstd::move(value));
    }

    static auto decode_pointer_token(ref<str> token) -> ::alloc::string::String {
        auto decoded = ::alloc::string::String::make();
        for (usize i = 0; i < token.size(); ++i) {
            if (token.data()[i] == '~' && i + 1 < token.size()) {
                if (token.data()[i + 1] == '0') {
                    decoded.push_back('~');
                    ++i;
                    continue;
                }
                if (token.data()[i + 1] == '1') {
                    decoded.push_back('/');
                    ++i;
                    continue;
                }
            }
            decoded.push_back(token.data()[i]);
        }
        return decoded;
    }

public:
    Value(const Value&)                = delete;
    Value& operator=(const Value&)     = delete;
    Value(Value&&) noexcept            = default;
    Value& operator=(Value&&) noexcept = default;

    auto clone() const -> Value;

    void clone_from(Value& source) { *this = source.clone(); }

    [[nodiscard]]
    auto is_object() const noexcept -> bool {
        return is_Object();
    }
    [[nodiscard]]
    auto is_array() const noexcept -> bool {
        return is_Array();
    }
    [[nodiscard]]
    auto is_string() const noexcept -> bool {
        return is_String();
    }
    [[nodiscard]]
    auto is_number() const noexcept -> bool {
        return is_Number();
    }
    [[nodiscard]]
    auto is_boolean() const noexcept -> bool {
        return is_Bool();
    }
    [[nodiscard]]
    auto is_null() const noexcept -> bool {
        return is_Null();
    }

    [[nodiscard]]
    auto is_i64() const noexcept -> bool {
        return is_Number() && as_Number().value.is_i64();
    }
    [[nodiscard]]
    auto is_u64() const noexcept -> bool {
        return is_Number() && as_Number().value.is_u64();
    }
    [[nodiscard]]
    auto is_f64() const noexcept -> bool {
        return is_Number() && as_Number().value.is_f64();
    }

    [[nodiscard]]
    auto as_object() const noexcept -> Option<ref<Map>> {
        if (! is_Object()) return None();
        return Some(ref<Map>::from_raw_parts(rstd::addressof(as_Object().value)));
    }
    [[nodiscard]]
    auto as_object_mut() noexcept -> Option<mut_ref<Map>> {
        if (! is_Object()) return None();
        return Some(mut_ref<Map>::from_raw_parts(rstd::addressof(as_Object().value)));
    }
    [[nodiscard]]
    auto as_array() const noexcept -> Option<ref<json::Array>> {
        if (! is_Array()) return None();
        return Some(ref<json::Array>::from_raw_parts(rstd::addressof(as_Array().value)));
    }
    [[nodiscard]]
    auto as_array_mut() noexcept -> Option<mut_ref<json::Array>> {
        if (! is_Array()) return None();
        return Some(mut_ref<json::Array>::from_raw_parts(rstd::addressof(as_Array().value)));
    }
    [[nodiscard]]
    auto as_str() const noexcept -> Option<ref<str>> {
        if (! is_String()) return None();
        return Some(as_String().value.as_str());
    }
    [[nodiscard]]
    auto as_number() const noexcept -> Option<ref<json::Number>> {
        if (! is_Number()) return None();
        return Some(ref<json::Number>::from_raw_parts(rstd::addressof(as_Number().value)));
    }
    [[nodiscard]]
    auto as_i64() const noexcept -> Option<i64> {
        if (! is_Number()) return None();
        return as_Number().value.as_i64();
    }
    [[nodiscard]]
    auto as_u64() const noexcept -> Option<u64> {
        if (! is_Number()) return None();
        return as_Number().value.as_u64();
    }
    [[nodiscard]]
    auto as_f64() const noexcept -> Option<f64> {
        if (! is_Number()) return None();
        return as_Number().value.as_f64();
    }
    [[nodiscard]]
    auto as_bool() const noexcept -> Option<bool> {
        if (! is_Bool()) return None();
        return Some(bool(as_Bool().value));
    }
    [[nodiscard]]
    auto as_null() const noexcept -> Option<empty> {
        if (! is_Null()) return None();
        return Some(empty {});
    }

    [[nodiscard]]
    auto get(usize index) const noexcept -> Option<ref<Value>> {
        if (! is_Array() || index >= as_Array().value.len()) return None();
        return Some(ref<Value>::from_raw_parts(rstd::addressof(as_Array().value[index])));
    }
    [[nodiscard]]
    auto get_mut(usize index) noexcept -> Option<mut_ref<Value>> {
        if (! is_Array() || index >= as_Array().value.len()) return None();
        return Some(mut_ref<Value>::from_raw_parts(rstd::addressof(as_Array().value[index])));
    }
    [[nodiscard]]
    auto get(ref<str> key) const noexcept -> Option<ref<Value>> {
        if (! is_Object()) return None();
        return as_Object().value.get(key);
    }
    [[nodiscard]]
    auto get_mut(ref<str> key) noexcept -> Option<mut_ref<Value>> {
        if (! is_Object()) return None();
        return as_Object().value.get_mut(key);
    }
    [[nodiscard]]
    auto get(const ::alloc::string::String& key) const noexcept -> Option<ref<Value>> {
        return get(key.as_str());
    }
    [[nodiscard]]
    auto get_mut(const ::alloc::string::String& key) noexcept -> Option<mut_ref<Value>> {
        return get_mut(key.as_str());
    }

    [[nodiscard]]
    auto take() -> Value {
        auto old = rstd::move(*this);
        replace_Null();
        return old;
    }

    void sort_all_objects() {
        if (is_Array()) {
            for (auto& value : as_Array().value) value.sort_all_objects();
        } else if (is_Object()) {
            auto values = as_Object().value.values_mut();
            for (auto value = values.next(); value.is_some(); value = values.next()) {
                (**value).sort_all_objects();
            }
        }
    }

    [[nodiscard]]
    auto operator[](usize index) const noexcept -> const Value& {
        auto value = get(index);
        return value.is_some() ? **value : null_sentinel();
    }

    [[nodiscard]]
    auto operator[](ref<str> key) const noexcept -> const Value& {
        auto value = get(key);
        return value.is_some() ? **value : null_sentinel();
    }

    auto operator[](usize index) -> Value& {
        auto value = get_mut(index);
        if (value.is_none()) rstd::panic { "cannot access JSON array index" };
        return **value;
    }

    auto operator[](ref<str> key) -> Value& {
        if (is_Null()) replace_Object(Map::make());
        if (! is_Object()) rstd::panic { "cannot access JSON object key" };

        auto value = as_Object().value.get_mut(key);
        if (value.is_some()) return **value;

        auto owned = ::alloc::string::String::make(key);
        as_Object().value.insert(rstd::move(owned), Value::Null());
        return **as_Object().value.get_mut(key);
    }

    [[nodiscard]]
    auto pointer(ref<str> path) const -> Option<ref<Value>> {
        if (path.size() == 0) {
            return Some(ref<Value>::from_raw_parts(rstd::addressof(*this)));
        }
        if (path.data()[0] != '/') return None();

        const Value* current = this;
        usize        start   = 1;
        while (start <= path.size()) {
            usize end = start;
            while (end < path.size() && path.data()[end] != '/') ++end;
            auto token = ref<str>::from_raw_parts(path.data() + start, end - start);

            if (current->is_Object()) {
                if (str_::contains(token, "~")) {
                    auto decoded = decode_pointer_token(token);
                    auto next    = current->get(decoded.as_str());
                    if (next.is_none()) return None();
                    current = &**next;
                } else {
                    auto next = current->get(token);
                    if (next.is_none()) return None();
                    current = &**next;
                }
            } else if (current->is_Array()) {
                auto index = parse_array_index(token);
                if (index.is_none()) return None();
                auto next = current->get(*index);
                if (next.is_none()) return None();
                current = &**next;
            } else {
                return None();
            }

            if (end == path.size()) break;
            start = end + 1;
        }
        return Some(ref<Value>::from_raw_parts(current));
    }

    [[nodiscard]]
    auto pointer_mut(ref<str> path) -> Option<mut_ref<Value>> {
        if (path.size() == 0) {
            return Some(mut_ref<Value>::from_raw_parts(rstd::addressof(*this)));
        }
        if (path.data()[0] != '/') return None();

        Value* current = this;
        usize  start   = 1;
        while (start <= path.size()) {
            usize end = start;
            while (end < path.size() && path.data()[end] != '/') ++end;
            auto token = ref<str>::from_raw_parts(path.data() + start, end - start);

            if (current->is_Object()) {
                if (str_::contains(token, "~")) {
                    auto decoded = decode_pointer_token(token);
                    auto next    = current->get_mut(decoded.as_str());
                    if (next.is_none()) return None();
                    current = &**next;
                } else {
                    auto next = current->get_mut(token);
                    if (next.is_none()) return None();
                    current = &**next;
                }
            } else if (current->is_Array()) {
                auto index = parse_array_index(token);
                if (index.is_none()) return None();
                auto next = current->get_mut(*index);
                if (next.is_none()) return None();
                current = &**next;
            } else {
                return None();
            }

            if (end == path.size()) break;
            start = end + 1;
        }
        return Some(mut_ref<Value>::from_raw_parts(current));
    }
};

auto Value::clone() const -> Value {
    RSTD_MATCH(*this) {
        RSTD_CASE(Null) {
            return Value::Null();
        }
        RSTD_CASE(Bool, boolean) {
            return Value::Bool(boolean);
        }
        RSTD_CASE(Number, number) {
            return Value::Number(number);
        }
        RSTD_CASE(String, string) {
            return Value::String(string.clone());
        }
        RSTD_CASE(Array, array) {
            return Value::Array(array.clone());
        }
        RSTD_CASE(Object, object) {
            return Value::Object(object.clone());
        }
    }
    rstd::panic { "invalid JSON value" };
}

#undef RSTD_JSON_VALUE_VARIANTS

} // namespace rstd::json

namespace rstd
{

template<>
struct Impl<cmp::PartialEq<json::Value>, json::Value>
    : DefaultInImpl<cmp::PartialEq<json::Value>, json::Value> {
    auto eq(const json::Value& other) const noexcept -> bool {
        auto& value = this->self();
        if (value.tag() != other.tag()) return false;
        RSTD_MATCH(value) {
            RSTD_CASE(Null) {
                return true;
            }
            RSTD_CASE(Bool, boolean) {
                return boolean == other.as_Bool().value;
            }
            RSTD_CASE(Number, number) {
                return number == other.as_Number().value;
            }
            RSTD_CASE(String, string) {
                return string == other.as_String().value;
            }
            RSTD_CASE(Array, array) {
                return array == other.as_Array().value;
            }
            RSTD_CASE(Object, object) {
                return object == other.as_Object().value;
            }
        }
        return false;
    }
};

template<>
struct Impl<convert::From<bool>, json::Value> : ImplBase<json::Value> {
    static auto from(bool value) -> json::Value { return json::Value::Bool(value); }
};

#define RSTD_JSON_FROM_UNSIGNED(Type)                                                    \
    template<>                                                                           \
    struct Impl<convert::From<Type>, json::Value> : ImplBase<json::Value> {              \
        static auto from(Type value) -> json::Value {                                    \
            return json::Value::Number(json::Number::from_u64(static_cast<u64>(value))); \
        }                                                                                \
    };

#define RSTD_JSON_FROM_SIGNED(Type)                                                      \
    template<>                                                                           \
    struct Impl<convert::From<Type>, json::Value> : ImplBase<json::Value> {              \
        static auto from(Type value) -> json::Value {                                    \
            return json::Value::Number(json::Number::from_i64(static_cast<i64>(value))); \
        }                                                                                \
    };

RSTD_JSON_FROM_UNSIGNED(u8)
RSTD_JSON_FROM_UNSIGNED(u16)
RSTD_JSON_FROM_UNSIGNED(u32)
RSTD_JSON_FROM_UNSIGNED(u64)
RSTD_JSON_FROM_SIGNED(i8)
RSTD_JSON_FROM_SIGNED(i16)
RSTD_JSON_FROM_SIGNED(i32)
RSTD_JSON_FROM_SIGNED(i64)

#undef RSTD_JSON_FROM_UNSIGNED
#undef RSTD_JSON_FROM_SIGNED

template<>
struct Impl<convert::From<f64>, json::Value> : ImplBase<json::Value> {
    static auto from(f64 value) -> json::Value {
        auto number = json::Number::from_f64(value);
        if (number.is_none()) return json::Value::Null();
        return json::Value::Number(*number);
    }
};

template<>
struct Impl<convert::From<f32>, json::Value> : ImplBase<json::Value> {
    static auto from(f32 value) -> json::Value {
        return Impl<convert::From<f64>, json::Value>::from(static_cast<f64>(value));
    }
};

template<>
struct Impl<convert::From<json::Number>, json::Value> : ImplBase<json::Value> {
    static auto from(json::Number value) -> json::Value { return json::Value::Number(value); }
};

template<>
struct Impl<convert::From<::alloc::string::String>, json::Value> : ImplBase<json::Value> {
    static auto from(::alloc::string::String value) -> json::Value {
        return json::Value::String(rstd::move(value));
    }
};

template<>
struct Impl<convert::From<ref<str>>, json::Value> : ImplBase<json::Value> {
    static auto from(ref<str> value) -> json::Value {
        return json::Value::String(::alloc::string::String::make(value));
    }
};

template<>
struct Impl<convert::From<json::Array>, json::Value> : ImplBase<json::Value> {
    static auto from(json::Array value) -> json::Value {
        return json::Value::Array(rstd::move(value));
    }
};

template<>
struct Impl<convert::From<json::Map>, json::Value> : ImplBase<json::Value> {
    static auto from(json::Map value) -> json::Value {
        return json::Value::Object(rstd::move(value));
    }
};

template<>
struct Impl<convert::From<empty>, json::Value> : ImplBase<json::Value> {
    static auto from(empty) -> json::Value { return json::Value::Null(); }
};

template<typename T>
    requires Impled<json::Value, convert::From<T>>
struct Impl<convert::From<Option<T>>, json::Value> : ImplBase<json::Value> {
    static auto from(Option<T> value) -> json::Value {
        if (value.is_none()) return json::Value::Null();
        return Impl<convert::From<T>, json::Value>::from(value.unwrap());
    }
};

} // namespace rstd

namespace rstd::json
{

auto operator==(const Value& left, const Value& right) noexcept -> bool {
    return as<cmp::PartialEq<Value>>(left).eq(right);
}

template<mtp::is_int T>
    requires(! mtp::same_as<mtp::rm_cv<T>, bool>) && (sizeof(T) <= sizeof(u64))
auto operator==(const Value& left, T right) noexcept -> bool {
    if constexpr (static_cast<T>(-1) < static_cast<T>(0)) {
        return left.as_i64() == Some(static_cast<i64>(right));
    } else {
        return left.as_u64() == Some(static_cast<u64>(right));
    }
}

auto operator==(const Value& left, bool right) noexcept -> bool {
    return left.as_bool() == Some(bool(right));
}

auto operator==(const Value& left, f32 right) noexcept -> bool {
    auto value = left.as_f64();
    return value.is_some() && static_cast<f32>(*value) == right;
}

auto operator==(const Value& left, f64 right) noexcept -> bool {
    return left.as_f64() == Some(f64(right));
}

auto operator==(const Value& left, ref<str> right) noexcept -> bool {
    return left.as_str() == Some(right);
}

auto operator==(const Value& left, const ::alloc::string::String& right) noexcept -> bool {
    return left == right.as_str();
}

export template<mtp::is_int T>
    requires(! mtp::same_as<mtp::rm_cv<T>, bool>) && (sizeof(T) <= sizeof(u64))
auto operator==(T left, const Value& right) noexcept -> bool {
    return right == left;
}

export inline auto operator==(bool left, const Value& right) noexcept -> bool {
    return right == left;
}

export inline auto operator==(f32 left, const Value& right) noexcept -> bool {
    return right == left;
}

export inline auto operator==(f64 left, const Value& right) noexcept -> bool {
    return right == left;
}

export inline auto operator==(ref<str> left, const Value& right) noexcept -> bool {
    return right == left;
}

export inline auto operator==(const ::alloc::string::String& left, const Value& right) noexcept
    -> bool {
    return right == left;
}

} // namespace rstd::json
