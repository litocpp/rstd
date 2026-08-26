export module rstd.serde.token;
export import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

export namespace rstd::serde
{

enum class TokenKind : rstd::uint8_t
{
    Null,
    Unit,
    Bool,
    I64,
    U64,
    F64,
    String,
    Bytes,
    SomeStart,
    SomeEnd,
    SequenceStart,
    SequenceEnd,
    MapStart,
    MapEnd,
    EnumStart,
    EnumEnd,
};

class Token {
    TokenKind kind_;
    bool      bool_value_ {};
    i64       i64_value_ {};
    u64       u64_value_ {};
    f64       f64_value_ {};
    usize     len_ {};
    String    string_value_;
    Vec<u8>   bytes_value_;

    explicit Token(TokenKind kind);

public:
    Token(const Token&)                = delete;
    Token& operator=(const Token&)     = delete;
    Token(Token&&) noexcept            = default;
    Token& operator=(Token&&) noexcept = default;

    static auto null() -> Token;
    static auto boolean(bool value) -> Token;
    static auto signed_integer(i64 value) -> Token;
    static auto unsigned_integer(u64 value) -> Token;
    static auto floating(f64 value) -> Token;
    static auto string(ref<str> value) -> Token;
    static auto bytes(slice<u8> value) -> Token;
    static auto marker(TokenKind kind) -> Token;
    static auto container(TokenKind kind, usize len) -> Token;

    constexpr auto kind() const noexcept -> TokenKind { return kind_; }
    constexpr auto as_bool() const noexcept -> bool { return bool_value_; }
    constexpr auto as_i64() const noexcept -> i64 { return i64_value_; }
    constexpr auto as_u64() const noexcept -> u64 { return u64_value_; }
    constexpr auto as_f64() const noexcept -> f64 { return f64_value_; }
    constexpr auto len() const noexcept -> usize { return len_; }
    auto           as_str() const noexcept [[clang::lifetimebound]] -> ref<str>;
    auto           as_bytes() const noexcept [[clang::lifetimebound]] -> slice<u8>;
};

class TokenSerializer;
class TokenSequence;
class TokenMap;
class TokenEnumAccess;

class TokenSerializer {
    Vec<Token>* tokens_;
    usize       owned_open_compounds_ {};
    usize*      open_compounds_ { &owned_open_compounds_ };
    DataPath    path_;

public:
    using value_type    = empty;
    using error_type    = Error;
    using result_type   = Result<value_type, error_type>;
    using sequence_type = TokenSequence;
    using map_type      = TokenMap;

    explicit TokenSerializer(Vec<Token>& tokens);
    TokenSerializer(Vec<Token>& tokens, usize& open_compounds, DataPath path);

    auto is_complete() const noexcept -> bool;

    auto serialize_none() -> result_type;
    auto serialize_unit() -> result_type;
    auto serialize_bool(bool value) -> result_type;
    auto serialize_i64(i64 value) -> result_type;
    auto serialize_u64(u64 value) -> result_type;
    auto serialize_f64(f64 value) -> result_type;
    auto serialize_string(ref<str> value) -> result_type;
    auto serialize_bytes(slice<u8> value) -> result_type;

    template<typename T>
    auto serialize_some(const T& value) -> result_type {
        tokens_->push(Token::marker(TokenKind::SomeStart));
        auto child  = TokenSerializer(*tokens_, *open_compounds_, path_.clone());
        auto result = serde::serialize(child, value);
        if (result.is_err()) return result;
        tokens_->push(Token::marker(TokenKind::SomeEnd));
        return Ok(empty {});
    }

    template<typename T>
    auto serialize_newtype(ref<str>, const T& value) -> result_type {
        return serde::serialize(*this, value);
    }

    auto serialize_unit_variant(ref<str> variant) -> result_type;

    template<typename T>
    auto serialize_newtype_variant(ref<str> variant, const T& value) -> result_type {
        tokens_->push(Token::marker(TokenKind::EnumStart));
        tokens_->push(Token::string(variant));
        auto child  = TokenSerializer(*tokens_, *open_compounds_, path_.with_variant(variant));
        auto result = serde::serialize(child, value);
        if (result.is_err()) return result;
        tokens_->push(Token::marker(TokenKind::EnumEnd));
        return Ok(empty {});
    }

    auto begin_sequence(usize len) -> Result<TokenSequence, Error>;
    auto begin_map(usize len) -> Result<TokenMap, Error>;
    auto invalid_value(ref<str> message) const -> Error;
    template<typename Source>
    auto invalid_value_with_source(ref<str> message, Source source) const -> Error {
        return Error::invalid_value_with_source(path_.clone(), message, rstd::move(source));
    }
    auto missing_field(ref<str> field) const -> Error;
    auto unknown_field(ref<str> field) const -> Error;
    auto unknown_variant(ref<str> variant) const -> Error;
    auto duplicate_field(ref<str> field) const -> Error;
    auto invariant(ref<str> message) const -> Error;
    auto unsupported(ref<str> message) const -> Error;

    template<typename T>
    auto serialize_extension(ref<str>, const T&) -> result_type {
        return Err(unsupported("extension is unavailable in the token format"_str));
    }
};

class TokenSequence {
    Vec<Token>* tokens_;
    usize*      open_compounds_;
    DataPath    path_;
    usize       index_ {};
    bool        ended_ {};

public:
    using error_type = Error;

    TokenSequence(Vec<Token>& tokens, usize& open_compounds, DataPath path);

    template<typename T>
    auto element(const T& value) -> Result<empty, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "sequence already ended"_str));
        auto serializer = TokenSerializer(*tokens_, *open_compounds_, path_.with_index(index_));
        auto result     = serde::serialize(serializer, value);
        if (result.is_err()) return result;
        ++index_;
        return Ok(empty {});
    }

    auto end() -> Result<empty, Error>;
};

class TokenMap {
    Vec<Token>* tokens_;
    usize*      open_compounds_;
    DataPath    path_;
    DataPath    value_path_;
    bool        pending_ {};
    bool        ended_ {};

public:
    using error_type = Error;

    TokenMap(Vec<Token>& tokens, usize& open_compounds, DataPath path);

    template<typename T>
    auto key(const T& value) -> Result<empty, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_) return Err(Error::invariant(path_.clone(), "map value is missing"_str));
        if constexpr (mtp::same_as<mtp::rm_cvf<T>, String>) {
            value_path_ = path_.with_map_key(value.as_str());
        } else if constexpr (mtp::same_as<mtp::rm_cvf<T>, ref<str>>) {
            value_path_ = path_.with_map_key(value);
        } else {
            value_path_ = path_.clone();
        }
        auto serializer = TokenSerializer(*tokens_, *open_compounds_, path_.clone());
        auto result     = serde::serialize(serializer, value);
        if (result.is_err()) return result;
        pending_ = true;
        return Ok(empty {});
    }

    template<typename T>
    auto value(const T& value) -> Result<empty, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "map already ended"_str));
        if (! pending_) return Err(Error::invariant(path_.clone(), "map key is missing"_str));
        auto serializer = TokenSerializer(*tokens_, *open_compounds_, value_path_.clone());
        auto result     = serde::serialize(serializer, value);
        if (result.is_err()) return result;
        pending_ = false;
        return Ok(empty {});
    }

    auto end() -> Result<empty, Error>;
};

class TokenDeserializer;
class TokenSequenceAccess;
class TokenMapAccess;

class TokenDeserializer {
    slice<Token> tokens_;
    usize        owned_position_ {};
    usize*       position_ { &owned_position_ };
    DataPath     path_;

    TokenDeserializer(slice<Token> tokens, usize& position, DataPath path);

    auto        peek() const noexcept -> Option<ref<Token>>;
    auto        take(TokenKind expected, ValueKind expected_kind) -> Result<ref<Token>, Error>;
    static auto actual_kind(TokenKind kind) noexcept -> ValueKind;

    friend class TokenSequenceAccess;
    friend class TokenMapAccess;
    friend class TokenEnumAccess;

public:
    using error_type    = Error;
    using sequence_type = TokenSequenceAccess;
    using map_type      = TokenMapAccess;
    using enum_type     = TokenEnumAccess;

    explicit TokenDeserializer(slice<Token> tokens);
    TokenDeserializer(const TokenDeserializer&)                    = delete;
    TokenDeserializer(TokenDeserializer&&)                         = delete;
    auto operator=(const TokenDeserializer&) -> TokenDeserializer& = delete;
    auto operator=(TokenDeserializer&&) -> TokenDeserializer&      = delete;

    auto position() const noexcept -> usize;
    auto path() const noexcept [[clang::lifetimebound]] -> const DataPath&;
    auto invalid_value(ref<str> message) const -> Error;
    template<typename Source>
    auto invalid_value_with_source(ref<str> message, Source source) const -> Error {
        return Error::invalid_value_with_source(path_.clone(), message, rstd::move(source));
    }
    auto missing_field(ref<str> field) const -> Error;
    auto unknown_field(ref<str> field) const -> Error;
    auto unknown_variant(ref<str> variant) const -> Error;
    auto duplicate_field(ref<str> field) const -> Error;
    auto invariant(ref<str> message) const -> Error;
    auto unsupported(ref<str> message) const -> Error;

    template<typename T>
    auto deserialize_extension(ref<str>) -> Result<T, Error> {
        return Err(unsupported("extension is unavailable in the token format"_str));
    }

    auto deserialize_bool() -> Result<bool, Error>;
    auto deserialize_unit() -> Result<empty, Error>;
    auto deserialize_i64() -> Result<i64, Error>;
    auto deserialize_u64() -> Result<u64, Error>;
    auto deserialize_f64() -> Result<f64, Error>;
    auto deserialize_string() -> Result<String, Error>;
    auto deserialize_bytes() -> Result<Vec<u8>, Error>;

    template<typename T>
    auto deserialize_option() -> Result<Option<T>, Error> {
        auto token = peek();
        if (token.is_none()) return Err(Error::unexpected_end(path_.clone()));
        if (token->get().kind() == TokenKind::Null) {
            ++*position_;
            return Ok(None<T>());
        }
        if (token->get().kind() != TokenKind::SomeStart) {
            return Err(Error::type_mismatch(
                path_.clone(), ValueKind::Enum, actual_kind(token->get().kind())));
        }
        ++*position_;
        auto child = TokenDeserializer(tokens_, *position_, path_.clone());
        auto value = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        auto end = take(TokenKind::SomeEnd, ValueKind::Enum);
        if (end.is_err()) return Err(rstd::move(end).unwrap_err_unchecked());
        return Ok(Some(rstd::move(value).unwrap_unchecked()));
    }

    template<typename T>
    auto deserialize_newtype(ref<str>) -> Result<T, Error> {
        return serde::deserialize<T>(*this);
    }

    auto begin_sequence() -> Result<TokenSequenceAccess, Error>;
    auto begin_map() -> Result<TokenMapAccess, Error>;
    auto begin_enum() -> Result<TokenEnumAccess, Error>;
    auto ignore_value() -> Result<empty, Error>;
};

class TokenSequenceAccess {
    slice<Token> tokens_;
    usize*       position_;
    DataPath     path_;
    usize        index_ {};
    bool         ended_ {};

public:
    using error_type = Error;

    TokenSequenceAccess(slice<Token> tokens, usize& position, DataPath path);

    template<typename T>
    auto next() -> Result<Option<T>, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "sequence already ended"_str));
        if (*position_ >= tokens_.len()) return Err(Error::unexpected_end(path_.clone()));
        if (tokens_[*position_].kind() == TokenKind::SequenceEnd) return Ok(None<T>());
        auto child = TokenDeserializer(tokens_, *position_, path_.with_index(index_));
        auto value = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        ++index_;
        return Ok(Some(rstd::move(value).unwrap_unchecked()));
    }

    auto end() -> Result<empty, Error>;
};

class TokenMapAccess {
    slice<Token> tokens_;
    usize*       position_;
    DataPath     path_;
    DataPath     value_path_;
    bool         pending_ {};
    bool         ended_ {};

public:
    using error_type = Error;

    TokenMapAccess(slice<Token> tokens, usize& position, DataPath path);

    template<typename T>
    auto next_key() -> Result<Option<T>, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_) return Err(Error::invariant(path_.clone(), "map value is missing"_str));
        if (*position_ >= tokens_.len()) return Err(Error::unexpected_end(path_.clone()));
        if (tokens_[*position_].kind() == TokenKind::MapEnd) return Ok(None<T>());
        auto child = TokenDeserializer(tokens_, *position_, path_.clone());
        auto key   = serde::deserialize<T>(child);
        if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
        if constexpr (mtp::same_as<T, String>) {
            value_path_ = path_.with_map_key(key->as_str());
        } else {
            value_path_ = path_.clone();
        }
        pending_ = true;
        return Ok(Some(rstd::move(key).unwrap_unchecked()));
    }

    template<typename T>
    auto next_value() -> Result<T, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "map already ended"_str));
        if (! pending_) return Err(Error::invariant(path_.clone(), "map key is missing"_str));
        auto child = TokenDeserializer(tokens_, *position_, value_path_.clone());
        auto value = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        pending_ = false;
        return value;
    }

    auto ignore_value() -> Result<empty, Error>;
    auto end() -> Result<empty, Error>;
};

class TokenEnumAccess {
    slice<Token> tokens_;
    usize*       position_;
    DataPath     path_;
    String       variant_;
    bool         consumed_ {};

    auto finish() -> Result<empty, Error>;

public:
    using error_type = Error;

    TokenEnumAccess(slice<Token> tokens, usize& position, DataPath path, String variant);

    auto variant() const noexcept [[clang::lifetimebound]] -> ref<str>;

    auto unit() -> Result<empty, Error>;

    template<typename T>
    auto value() -> Result<T, Error> {
        if (consumed_) return Err(Error::invariant(path_.clone(), "enum already consumed"_str));
        auto child  = TokenDeserializer(tokens_, *position_, path_.with_variant(variant_.as_str()));
        auto result = serde::deserialize<T>(child);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        auto end = finish();
        if (end.is_err()) return Err(rstd::move(end).unwrap_err_unchecked());
        return result;
    }
};

template<typename T>
auto to_tokens(const T& value) -> Result<Vec<Token>, Error> {
    auto tokens     = Vec<Token>::make();
    auto serializer = TokenSerializer(tokens);
    auto result     = serde::serialize(serializer, value);
    if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    if (! serializer.is_complete()) {
        return Err(Error::invariant(DataPath(), "serde compound was not ended"_str));
    }
    return Ok(rstd::move(tokens));
}

template<typename T>
auto from_tokens(slice<Token> tokens) -> Result<T, Error> {
    auto deserializer = TokenDeserializer(tokens);
    auto result       = serde::deserialize<T>(deserializer);
    if (result.is_err()) return result;
    if (deserializer.position() != tokens.len()) {
        return Err(Error::invariant(DataPath(), "trailing serde tokens"_str));
    }
    return result;
}

} // namespace rstd::serde
