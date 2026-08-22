export module rstd.serde:record;
export import :containers;

using namespace rstd::prelude;
using ::alloc::string::String;

export namespace rstd::serde
{

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
    ref<str>  name_;
    Option<T> value_;

public:
    explicit RequiredField(ref<str> name): name_(name) {}

    auto matches(ref<str> name) const noexcept -> bool { return name == name_; }

    template<typename Deserializer, typename Map>
    auto assign(Deserializer& deserializer, Map& map)
        -> Result<empty, typename Deserializer::error_type> {
        if (value_.is_some()) return Err(deserializer.duplicate_field(name_));
        auto value = map.template next_value<T>();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        value_ = Some(rstd::move(value).unwrap_unchecked());
        return Ok(empty {});
    }

    template<typename Deserializer>
    auto take(Deserializer& deserializer) -> Result<T, typename Deserializer::error_type> {
        if (value_.is_none()) return Err(deserializer.missing_field(name_));
        return Ok(rstd::move(value_).unwrap_unchecked());
    }
};

template<typename T>
class OptionalField {
    ref<str>  name_;
    Option<T> value_;
    bool      present_ {};

public:
    explicit OptionalField(ref<str> name): name_(name) {}

    auto matches(ref<str> name) const noexcept -> bool { return name == name_; }

    template<typename Deserializer, typename Map>
    auto assign(Deserializer& deserializer, Map& map)
        -> Result<empty, typename Deserializer::error_type> {
        if (present_) return Err(deserializer.duplicate_field(name_));
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
    ref<str>  name_;
    Option<T> value_;
    T         default_;

public:
    DefaultedField(ref<str> name, T default_value)
        : name_(name), default_(rstd::move(default_value)) {}

    auto matches(ref<str> name) const noexcept -> bool { return name == name_; }

    template<typename Deserializer, typename Map>
    auto assign(Deserializer& deserializer, Map& map)
        -> Result<empty, typename Deserializer::error_type> {
        if (value_.is_some()) return Err(deserializer.duplicate_field(name_));
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

} // namespace rstd::serde
