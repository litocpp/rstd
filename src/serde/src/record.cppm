export module rstd.serde:record;
export import :containers;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

export namespace rstd::serde
{

enum class UnknownFieldPolicy : rstd::uint8_t
{
    Reject,
    Ignore,
};

class FieldNames {
    String      canonical_;
    Vec<String> aliases_;

public:
    explicit FieldNames(ref<str> canonical): canonical_(String::make(canonical)) {}
    FieldNames(ref<str> canonical, slice<ref<str>> aliases)
        : canonical_(String::make(canonical)), aliases_(Vec<String>::with_capacity(aliases.len())) {
        for (auto alias : aliases) aliases_.push(String::make(alias));
    }

    auto canonical() const noexcept [[clang::lifetimebound]] -> ref<str> {
        return canonical_.as_str();
    }
    auto overlaps(const FieldNames& other) const noexcept -> bool {
        if (other.matches(canonical_.as_str())) return true;
        for (const auto& alias : aliases_) {
            if (other.matches(alias.as_str())) return true;
        }
        return false;
    }
    auto matches(ref<str> name) const noexcept -> bool {
        if (name == canonical_.as_str()) return true;
        for (const auto& alias : aliases_) {
            if (name == alias.as_str()) return true;
        }
        return false;
    }
};

template<typename Deserializer, typename Function>
auto deserialize_record(Deserializer& deserializer, Function function)
    -> Result<empty, typename Deserializer::error_type> {
    auto map = deserializer.begin_map();
    if (map.is_err()) return Err(rstd::move(map).unwrap_err_unchecked());
    auto input = rstd::move(map).unwrap_unchecked();
    for (;;) {
        auto key = input.template next_key<String>();
        if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
        auto optional = rstd::move(key).unwrap_unchecked();
        if (optional.is_none()) break;
        auto name   = rstd::move(optional).unwrap_unchecked();
        auto result = function(name.as_str(), input);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    }
    return input.end();
}

template<typename Map, typename T>
auto field(Map& map, ref<str> name, const T& value) -> Result<empty, typename Map::error_type> {
    auto key = map.key(name);
    if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
    return map.value(value);
}

template<typename T>
class RequiredField {
    FieldNames names_;
    Option<T>  value_;

public:
    explicit RequiredField(ref<str> name): names_(name) {}
    RequiredField(ref<str> name, slice<ref<str>> aliases): names_(name, aliases) {}

    auto matches(ref<str> name) const noexcept -> bool { return names_.matches(name); }
    auto names() const noexcept [[clang::lifetimebound]] -> const FieldNames& { return names_; }

    template<typename Deserializer, typename Map>
    auto assign(Deserializer& deserializer, Map& map)
        -> Result<empty, typename Deserializer::error_type> {
        if (value_.is_some()) return Err(deserializer.duplicate_field(names_.canonical()));
        auto value = map.template next_value<T>();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        value_ = Some(rstd::move(value).unwrap_unchecked());
        return Ok(empty {});
    }

    template<typename Deserializer>
    auto take(Deserializer& deserializer) -> Result<T, typename Deserializer::error_type> {
        if (value_.is_none()) return Err(deserializer.missing_field(names_.canonical()));
        return Ok(rstd::move(value_).unwrap_unchecked());
    }
};

template<typename T>
class OptionalField {
    FieldNames names_;
    Option<T>  value_;
    bool       present_ {};

public:
    explicit OptionalField(ref<str> name): names_(name) {}
    OptionalField(ref<str> name, slice<ref<str>> aliases): names_(name, aliases) {}

    auto matches(ref<str> name) const noexcept -> bool { return names_.matches(name); }
    auto names() const noexcept [[clang::lifetimebound]] -> const FieldNames& { return names_; }

    template<typename Deserializer, typename Map>
    auto assign(Deserializer& deserializer, Map& map)
        -> Result<empty, typename Deserializer::error_type> {
        if (present_) return Err(deserializer.duplicate_field(names_.canonical()));
        auto value = map.template next_value<T>();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        value_   = Some(rstd::move(value).unwrap_unchecked());
        present_ = true;
        return Ok(empty {});
    }

    auto take() -> Option<T> { return rstd::move(value_); }
};

template<typename T>
class DefaultedField {
    FieldNames names_;
    Option<T>  value_;
    T          default_;

public:
    DefaultedField(ref<str> name, T default_value)
        : names_(name), default_(rstd::move(default_value)) {}
    DefaultedField(ref<str> name, slice<ref<str>> aliases, T default_value)
        : names_(name, aliases), default_(rstd::move(default_value)) {}

    auto matches(ref<str> name) const noexcept -> bool { return names_.matches(name); }
    auto names() const noexcept [[clang::lifetimebound]] -> const FieldNames& { return names_; }

    template<typename Deserializer, typename Map>
    auto assign(Deserializer& deserializer, Map& map)
        -> Result<empty, typename Deserializer::error_type> {
        if (value_.is_some()) return Err(deserializer.duplicate_field(names_.canonical()));
        auto value = map.template next_value<T>();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        value_ = Some(rstd::move(value).unwrap_unchecked());
        return Ok(empty {});
    }

    auto take() -> T {
        if (value_.is_some()) return rstd::move(value_).unwrap_unchecked();
        return rstd::move(default_);
    }
};

template<typename Field, typename Deserializer, typename Map>
concept RecordField = requires(Field&        field,
                               const Field&  const_field,
                               Deserializer& deserializer,
                               Map&          map,
                               ref<str>      name) {
    { const_field.matches(name) } -> mtp::same_as<bool>;
    { const_field.names() } -> mtp::same_as<const FieldNames&>;
    {
        field.assign(deserializer, map)
    } -> mtp::same_as<Result<empty, typename Deserializer::error_type>>;
};

template<typename Deserializer, typename First, typename... Rest>
auto validate_record_fields(Deserializer& deserializer, const First& first, const Rest&... rest)
    -> Result<empty, typename Deserializer::error_type> {
    if constexpr (sizeof...(Rest) > 0) {
        if ((first.names().overlaps(rest.names()) || ...)) {
            return Err(deserializer.invariant("record field names overlap"_str));
        }
        return validate_record_fields(deserializer, rest...);
    }
    return Ok(empty {});
}

template<typename Deserializer, typename Map, typename First, typename... Rest>
auto assign_record_field(Deserializer& deserializer,
                         Map&          map,
                         ref<str>      name,
                         First&        first,
                         Rest&... rest) -> Result<bool, typename Deserializer::error_type> {
    if (first.matches(name)) {
        auto assigned = first.assign(deserializer, map);
        if (assigned.is_err()) return Err(rstd::move(assigned).unwrap_err_unchecked());
        return Ok(true);
    }
    if constexpr (sizeof...(Rest) > 0) {
        return assign_record_field(deserializer, map, name, rest...);
    }
    return Ok(false);
}

template<typename Deserializer, typename... Fields>
    requires(sizeof...(Fields) > 0) &&
            (RecordField<Fields, Deserializer, typename Deserializer::map_type> && ...)
auto deserialize_record(Deserializer& deserializer, UnknownFieldPolicy policy, Fields&... fields)
    -> Result<empty, typename Deserializer::error_type> {
    auto valid = validate_record_fields(deserializer, fields...);
    if (valid.is_err()) return Err(rstd::move(valid).unwrap_err_unchecked());
    auto map = deserializer.begin_map();
    if (map.is_err()) return Err(rstd::move(map).unwrap_err_unchecked());
    auto input = rstd::move(map).unwrap_unchecked();
    for (;;) {
        auto key = input.template next_key<String>();
        if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
        auto optional = rstd::move(key).unwrap_unchecked();
        if (optional.is_none()) break;
        auto name    = rstd::move(optional).unwrap_unchecked();
        auto handled = assign_record_field(deserializer, input, name.as_str(), fields...);
        if (handled.is_err()) return Err(rstd::move(handled).unwrap_err_unchecked());
        if (*handled) continue;
        if (policy == UnknownFieldPolicy::Reject) {
            return Err(deserializer.unknown_field(name.as_str()));
        }
        auto ignored = input.ignore_value();
        if (ignored.is_err()) return Err(rstd::move(ignored).unwrap_err_unchecked());
    }
    return input.end();
}

} // namespace rstd::serde
