module;
#include <rstd/enum.hpp>

export module rstd.toml:value;
export import :datetime;
export import rstd.alloc;

namespace rstd::toml
{

export class Value;
export auto operator==(const Value& left, const Value& right) noexcept -> bool;

export using Array = ::alloc::vec::Vec<Value>;
export using Table = ::alloc::collections::BTreeMap<::alloc::string::String, Value>;

export class Value : public rstd::DefaultInClass<Value, rstd::clone::Clone> {
    RSTD_ENUM(Value,
              (Integer, (rstd::i64 value;)),
              (Float, (rstd::f64 value;)),
              (Boolean, (bool value;)),
              (String, (::alloc::string::String value;)),
              (OffsetDateTime, (rstd::toml::OffsetDateTime value;)),
              (LocalDateTime, (rstd::toml::LocalDateTime value;)),
              (LocalDate, (rstd::toml::LocalDate value;)),
              (LocalTime, (rstd::toml::LocalTime value;)),
              (Array, (rstd::toml::Array value;)),
              (Table, (rstd::toml::Table value;)))

public:
    Value() noexcept
        : rstd_enum_choice_(
              rstd_enum_choice_type::template with<Tag::Table>(rstd::toml::Table::make())) {}

    Value(const Value&)                = delete;
    Value& operator=(const Value&)     = delete;
    Value(Value&&) noexcept            = default;
    Value& operator=(Value&&) noexcept = default;

    auto clone() const -> Value;

    [[nodiscard]]
    auto is_integer() const noexcept -> bool {
        return is_Integer();
    }
    [[nodiscard]]
    auto is_float() const noexcept -> bool {
        return is_Float();
    }
    [[nodiscard]]
    auto is_boolean() const noexcept -> bool {
        return is_Boolean();
    }
    [[nodiscard]]
    auto is_string() const noexcept -> bool {
        return is_String();
    }
    [[nodiscard]]
    auto is_offset_datetime() const noexcept -> bool {
        return is_OffsetDateTime();
    }
    [[nodiscard]]
    auto is_local_datetime() const noexcept -> bool {
        return is_LocalDateTime();
    }
    [[nodiscard]]
    auto is_local_date() const noexcept -> bool {
        return is_LocalDate();
    }
    [[nodiscard]]
    auto is_local_time() const noexcept -> bool {
        return is_LocalTime();
    }
    [[nodiscard]]
    auto is_array() const noexcept -> bool {
        return is_Array();
    }
    [[nodiscard]]
    auto is_table() const noexcept -> bool {
        return is_Table();
    }

    [[nodiscard]]
    auto as_integer() const noexcept -> Option<i64> {
        if (! is_Integer()) return None();
        return Some<i64>(as_Integer().value);
    }
    [[nodiscard]]
    auto as_float() const noexcept -> Option<f64> {
        if (! is_Float()) return None();
        return Some<f64>(as_Float().value);
    }
    [[nodiscard]]
    auto as_bool() const noexcept -> Option<bool> {
        return is_Boolean() ? Some(bool(as_Boolean().value)) : None();
    }
    [[nodiscard]]
    auto as_str() const noexcept [[clang::lifetimebound]] -> Option<ref<str>> {
        return is_String() ? Some(as_String().value.as_str()) : None();
    }
    [[nodiscard]]
    auto as_offset_datetime() const noexcept -> Option<rstd::toml::OffsetDateTime> {
        if (! is_OffsetDateTime()) return None();
        return Some<rstd::toml::OffsetDateTime>(as_OffsetDateTime().value);
    }
    [[nodiscard]]
    auto as_local_datetime() const noexcept -> Option<rstd::toml::LocalDateTime> {
        if (! is_LocalDateTime()) return None();
        return Some<rstd::toml::LocalDateTime>(as_LocalDateTime().value);
    }
    [[nodiscard]]
    auto as_local_date() const noexcept -> Option<rstd::toml::LocalDate> {
        if (! is_LocalDate()) return None();
        return Some<rstd::toml::LocalDate>(as_LocalDate().value);
    }
    [[nodiscard]]
    auto as_local_time() const noexcept -> Option<rstd::toml::LocalTime> {
        if (! is_LocalTime()) return None();
        return Some<rstd::toml::LocalTime>(as_LocalTime().value);
    }
    [[nodiscard]]
    auto as_array() const noexcept [[clang::lifetimebound]] -> Option<ref<rstd::toml::Array>> {
        if (! is_Array()) return None();
        return Some(ref<rstd::toml::Array>::from_raw_parts(rstd::addressof(as_Array().value)));
    }
    [[nodiscard]]
    auto as_array_mut() noexcept [[clang::lifetimebound]] -> Option<mut_ref<rstd::toml::Array>> {
        if (! is_Array()) return None();
        return Some(mut_ref<rstd::toml::Array>::from_raw_parts(rstd::addressof(as_Array().value)));
    }
    [[nodiscard]]
    auto as_table() const noexcept [[clang::lifetimebound]] -> Option<ref<rstd::toml::Table>> {
        if (! is_Table()) return None();
        return Some(ref<rstd::toml::Table>::from_raw_parts(rstd::addressof(as_Table().value)));
    }
    [[nodiscard]]
    auto as_table_mut() noexcept [[clang::lifetimebound]] -> Option<mut_ref<rstd::toml::Table>> {
        if (! is_Table()) return None();
        return Some(mut_ref<rstd::toml::Table>::from_raw_parts(rstd::addressof(as_Table().value)));
    }
    [[nodiscard]]
    auto get(ref<str> key) const noexcept [[clang::lifetimebound]] -> Option<ref<Value>> {
        return is_Table() ? as_Table().value.get(key) : None();
    }
    [[nodiscard]]
    auto get_mut(ref<str> key) noexcept [[clang::lifetimebound]] -> Option<mut_ref<Value>> {
        return is_Table() ? as_Table().value.get_mut(key) : None();
    }
    [[nodiscard]]
    auto get(usize index) const noexcept [[clang::lifetimebound]] -> Option<ref<Value>> {
        if (! is_Array() || index >= as_Array().value.len()) return None();
        return Some(ref<Value>::from_raw_parts(rstd::addressof(as_Array().value[index])));
    }
    [[nodiscard]]
    auto get_mut(usize index) noexcept [[clang::lifetimebound]] -> Option<mut_ref<Value>> {
        if (! is_Array() || index >= as_Array().value.len()) return None();
        return Some(mut_ref<Value>::from_raw_parts(rstd::addressof(as_Array().value[index])));
    }
};

auto Value::clone() const -> Value {
    RSTD_MATCH(*this) {
        RSTD_CASE(Integer, value) {
            return Value::Integer(value);
        }
        RSTD_CASE(Float, value) {
            return Value::Float(value);
        }
        RSTD_CASE(Boolean, value) {
            return Value::Boolean(value);
        }
        RSTD_CASE(String, value) {
            return Value::String(value.clone());
        }
        RSTD_CASE(OffsetDateTime, value) {
            return Value::OffsetDateTime(value);
        }
        RSTD_CASE(LocalDateTime, value) {
            return Value::LocalDateTime(value);
        }
        RSTD_CASE(LocalDate, value) {
            return Value::LocalDate(value);
        }
        RSTD_CASE(LocalTime, value) {
            return Value::LocalTime(value);
        }
        RSTD_CASE(Array, value) {
            return Value::Array(value.clone());
        }
        RSTD_CASE(Table, value) {
            return Value::Table(value.clone());
        }
    }
    rstd::panic { "invalid TOML value" };
}

} // namespace rstd::toml

namespace rstd
{

template<>
struct Impl<cmp::PartialEq<toml::Value>, toml::Value>
    : DefaultInImpl<cmp::PartialEq<toml::Value>, toml::Value> {
    auto eq(const toml::Value& other) const noexcept -> bool {
        const auto& value = this->self();
        if (value.tag() != other.tag()) return false;
        RSTD_MATCH(value) {
            RSTD_CASE(Integer, item) {
                return item == other.as_Integer().value;
            }
            RSTD_CASE(Float, item) {
                return item == other.as_Float().value;
            }
            RSTD_CASE(Boolean, item) {
                return item == other.as_Boolean().value;
            }
            RSTD_CASE(String, item) {
                return item == other.as_String().value;
            }
            RSTD_CASE(OffsetDateTime, item) {
                return item == other.as_OffsetDateTime().value;
            }
            RSTD_CASE(LocalDateTime, item) {
                return item == other.as_LocalDateTime().value;
            }
            RSTD_CASE(LocalDate, item) {
                return item == other.as_LocalDate().value;
            }
            RSTD_CASE(LocalTime, item) {
                return item == other.as_LocalTime().value;
            }
            RSTD_CASE(Array, item) {
                return item == other.as_Array().value;
            }
            RSTD_CASE(Table, item) {
                return item == other.as_Table().value;
            }
        }
        return false;
    }
};

} // namespace rstd

namespace rstd::toml
{

auto operator==(const Value& left, const Value& right) noexcept -> bool {
    return rstd::as<rstd::cmp::PartialEq<Value>>(left).eq(right);
}

} // namespace rstd::toml
