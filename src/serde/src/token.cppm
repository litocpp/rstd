export module rstd.serde:token;
export import :record;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

export namespace rstd::serde
{

enum class TokenKind : rstd::uint8_t
{
    Null,
    Bool,
    I64,
    U64,
    F64,
    String,
    SomeStart,
    SomeEnd,
    SequenceStart,
    SequenceEnd,
    MapStart,
    MapEnd,
};

class Token {
    TokenKind kind_;
    bool      bool_value_ {};
    i64       i64_value_ {};
    u64       u64_value_ {};
    f64       f64_value_ {};
    usize     len_ {};
    String    string_value_;

    explicit Token(TokenKind kind): kind_(kind) {}

public:
    Token(const Token&)                = delete;
    Token& operator=(const Token&)     = delete;
    Token(Token&&) noexcept            = default;
    Token& operator=(Token&&) noexcept = default;

    static auto null() -> Token { return Token(TokenKind::Null); }
    static auto boolean(bool value) -> Token {
        auto token        = Token(TokenKind::Bool);
        token.bool_value_ = value;
        return token;
    }
    static auto signed_integer(i64 value) -> Token {
        auto token       = Token(TokenKind::I64);
        token.i64_value_ = value;
        return token;
    }
    static auto unsigned_integer(u64 value) -> Token {
        auto token       = Token(TokenKind::U64);
        token.u64_value_ = value;
        return token;
    }
    static auto floating(f64 value) -> Token {
        auto token       = Token(TokenKind::F64);
        token.f64_value_ = value;
        return token;
    }
    static auto string(ref<str> value) -> Token {
        auto token          = Token(TokenKind::String);
        token.string_value_ = String::make(value);
        return token;
    }
    static auto marker(TokenKind kind) -> Token { return Token(kind); }
    static auto container(TokenKind kind, usize len) -> Token {
        auto token = Token(kind);
        token.len_ = len;
        return token;
    }

    constexpr auto kind() const noexcept -> TokenKind { return kind_; }
    constexpr auto as_bool() const noexcept -> bool { return bool_value_; }
    constexpr auto as_i64() const noexcept -> i64 { return i64_value_; }
    constexpr auto as_u64() const noexcept -> u64 { return u64_value_; }
    constexpr auto as_f64() const noexcept -> f64 { return f64_value_; }
    constexpr auto len() const noexcept -> usize { return len_; }
    auto           as_str() const noexcept [[clang::lifetimebound]] -> ref<str> {
        return string_value_.as_str();
    }
};

class TokenSerializer;
class TokenSequence;
class TokenMap;

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

    explicit TokenSerializer(Vec<Token>& tokens): tokens_(&tokens) {}
    TokenSerializer(Vec<Token>& tokens, usize& open_compounds, DataPath path)
        : tokens_(&tokens), open_compounds_(&open_compounds), path_(rstd::move(path)) {}

    auto is_complete() const noexcept -> bool { return *open_compounds_ == usize(); }

    auto serialize_none() -> result_type;
    auto serialize_bool(bool value) -> result_type;
    auto serialize_i64(i64 value) -> result_type;
    auto serialize_u64(u64 value) -> result_type;
    auto serialize_f64(f64 value) -> result_type;
    auto serialize_string(ref<str> value) -> result_type;

    template<typename T>
    auto serialize_some(const T& value) -> result_type {
        tokens_->push(Token::marker(TokenKind::SomeStart));
        auto child  = TokenSerializer(*tokens_, *open_compounds_, path_.clone());
        auto result = serde::serialize(child, value);
        if (result.is_err()) return result;
        tokens_->push(Token::marker(TokenKind::SomeEnd));
        return Ok(empty {});
    }

    auto begin_sequence(usize len) -> Result<TokenSequence, Error>;
    auto begin_map(usize len) -> Result<TokenMap, Error>;
    auto invalid_value(ref<str> message) const -> Error {
        return Error::invalid_value(path_.clone(), message);
    }
    template<typename Source>
    auto invalid_value_with_source(ref<str> message, Source source) const -> Error {
        return Error::invalid_value_with_source(path_.clone(), message, rstd::move(source));
    }
    auto missing_field(ref<str> field) const -> Error {
        return Error::missing_field(path_.clone(), field);
    }
    auto unknown_field(ref<str> field) const -> Error {
        return Error::unknown_field(path_.clone(), field);
    }
    auto duplicate_field(ref<str> field) const -> Error {
        return Error::duplicate_field(path_.clone(), field);
    }
    auto unsupported(ref<str> message) const -> Error {
        return Error::unsupported(path_.clone(), message);
    }

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

    TokenSequence(Vec<Token>& tokens, usize& open_compounds, DataPath path)
        : tokens_(&tokens), open_compounds_(&open_compounds), path_(rstd::move(path)) {}

    template<typename T>
    auto element(const T& value) -> Result<empty, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "sequence already ended"_str));
        auto serializer = TokenSerializer(*tokens_, *open_compounds_, path_.with_index(index_));
        auto result     = serde::serialize(serializer, value);
        if (result.is_err()) return result;
        ++index_;
        return Ok(empty {});
    }

    auto end() -> Result<empty, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "sequence already ended"_str));
        ended_ = true;
        --*open_compounds_;
        tokens_->push(Token::marker(TokenKind::SequenceEnd));
        return Ok(empty {});
    }
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

    TokenMap(Vec<Token>& tokens, usize& open_compounds, DataPath path)
        : tokens_(&tokens),
          open_compounds_(&open_compounds),
          path_(path.clone()),
          value_path_(rstd::move(path)) {}

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

    auto end() -> Result<empty, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_) return Err(Error::invariant(path_.clone(), "map value is missing"_str));
        ended_ = true;
        --*open_compounds_;
        tokens_->push(Token::marker(TokenKind::MapEnd));
        return Ok(empty {});
    }
};

inline auto TokenSerializer::serialize_none() -> result_type {
    tokens_->push(Token::null());
    return Ok(empty {});
}

inline auto TokenSerializer::serialize_bool(bool value) -> result_type {
    tokens_->push(Token::boolean(value));
    return Ok(empty {});
}

inline auto TokenSerializer::serialize_i64(i64 value) -> result_type {
    tokens_->push(Token::signed_integer(value));
    return Ok(empty {});
}

inline auto TokenSerializer::serialize_u64(u64 value) -> result_type {
    tokens_->push(Token::unsigned_integer(value));
    return Ok(empty {});
}

inline auto TokenSerializer::serialize_f64(f64 value) -> result_type {
    tokens_->push(Token::floating(value));
    return Ok(empty {});
}

inline auto TokenSerializer::serialize_string(ref<str> value) -> result_type {
    tokens_->push(Token::string(value));
    return Ok(empty {});
}

inline auto TokenSerializer::begin_sequence(usize len) -> Result<TokenSequence, Error> {
    tokens_->push(Token::container(TokenKind::SequenceStart, len));
    ++*open_compounds_;
    return Ok(TokenSequence(*tokens_, *open_compounds_, path_.clone()));
}

inline auto TokenSerializer::begin_map(usize len) -> Result<TokenMap, Error> {
    tokens_->push(Token::container(TokenKind::MapStart, len));
    ++*open_compounds_;
    return Ok(TokenMap(*tokens_, *open_compounds_, path_.clone()));
}

class TokenDeserializer;
class TokenSequenceAccess;
class TokenMapAccess;

class TokenDeserializer {
    slice<Token> tokens_;
    usize        owned_position_ {};
    usize*       position_ { &owned_position_ };
    DataPath     path_;

    TokenDeserializer(slice<Token> tokens, usize& position, DataPath path)
        : tokens_(tokens), position_(&position), path_(rstd::move(path)) {}

    auto peek() const noexcept -> Option<ref<Token>> {
        if (*position_ >= tokens_.len()) return None();
        return Some(ref<Token>::from_raw_parts(tokens_.as_raw_ptr() + position_->to_primitive()));
    }

    auto take(TokenKind expected, ValueKind expected_kind) -> Result<ref<Token>, Error> {
        auto token = peek();
        if (token.is_none()) return Err(Error::unexpected_end(path_.clone()));
        if (token->get().kind() != expected) {
            return Err(Error::type_mismatch(
                path_.clone(), expected_kind, actual_kind(token->get().kind())));
        }
        ++*position_;
        return Ok(*token);
    }

    static auto actual_kind(TokenKind kind) noexcept -> ValueKind {
        switch (kind) {
        case TokenKind::Null: return ValueKind::Null;
        case TokenKind::Bool: return ValueKind::Boolean;
        case TokenKind::I64: return ValueKind::SignedInteger;
        case TokenKind::U64: return ValueKind::UnsignedInteger;
        case TokenKind::F64: return ValueKind::Float;
        case TokenKind::String: return ValueKind::String;
        case TokenKind::SomeStart:
        case TokenKind::SomeEnd: return ValueKind::Enum;
        case TokenKind::SequenceStart:
        case TokenKind::SequenceEnd: return ValueKind::Sequence;
        case TokenKind::MapStart:
        case TokenKind::MapEnd: return ValueKind::Map;
        }
        rstd::unreachable();
    }

    friend class TokenSequenceAccess;
    friend class TokenMapAccess;

public:
    using error_type    = Error;
    using sequence_type = TokenSequenceAccess;
    using map_type      = TokenMapAccess;

    explicit TokenDeserializer(slice<Token> tokens): tokens_(tokens) {}
    TokenDeserializer(const TokenDeserializer&)                    = delete;
    TokenDeserializer(TokenDeserializer&&)                         = delete;
    auto operator=(const TokenDeserializer&) -> TokenDeserializer& = delete;
    auto operator=(TokenDeserializer&&) -> TokenDeserializer&      = delete;

    auto position() const noexcept -> usize { return *position_; }
    auto path() const noexcept [[clang::lifetimebound]] -> const DataPath& { return path_; }
    auto invalid_value(ref<str> message) const -> Error {
        return Error::invalid_value(path_.clone(), message);
    }
    template<typename Source>
    auto invalid_value_with_source(ref<str> message, Source source) const -> Error {
        return Error::invalid_value_with_source(path_.clone(), message, rstd::move(source));
    }
    auto missing_field(ref<str> field) const -> Error {
        return Error::missing_field(path_.clone(), field);
    }
    auto unknown_field(ref<str> field) const -> Error {
        return Error::unknown_field(path_.clone(), field);
    }
    auto duplicate_field(ref<str> field) const -> Error {
        return Error::duplicate_field(path_.clone(), field);
    }
    auto unsupported(ref<str> message) const -> Error {
        return Error::unsupported(path_.clone(), message);
    }

    template<typename T>
    auto deserialize_extension(ref<str>) -> Result<T, Error> {
        return Err(unsupported("extension is unavailable in the token format"_str));
    }

    auto deserialize_bool() -> Result<bool, Error> {
        auto token = take(TokenKind::Bool, ValueKind::Boolean);
        if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
        return Ok(token->get().as_bool());
    }
    auto deserialize_i64() -> Result<i64, Error> {
        auto token = take(TokenKind::I64, ValueKind::SignedInteger);
        if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
        return Ok(token->get().as_i64());
    }
    auto deserialize_u64() -> Result<u64, Error> {
        auto token = take(TokenKind::U64, ValueKind::UnsignedInteger);
        if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
        return Ok(token->get().as_u64());
    }
    auto deserialize_f64() -> Result<f64, Error> {
        auto token = take(TokenKind::F64, ValueKind::Float);
        if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
        return Ok(token->get().as_f64());
    }
    auto deserialize_string() -> Result<String, Error> {
        auto token = take(TokenKind::String, ValueKind::String);
        if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
        return Ok(String::make(token->get().as_str()));
    }

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

    auto begin_sequence() -> Result<TokenSequenceAccess, Error>;
    auto begin_map() -> Result<TokenMapAccess, Error>;
};

class TokenSequenceAccess {
    slice<Token> tokens_;
    usize*       position_;
    DataPath     path_;
    usize        index_ {};
    bool         ended_ {};

public:
    TokenSequenceAccess(slice<Token> tokens, usize& position, DataPath path)
        : tokens_(tokens), position_(&position), path_(rstd::move(path)) {}

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

    auto end() -> Result<empty, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "sequence already ended"_str));
        if (*position_ >= tokens_.len()) return Err(Error::unexpected_end(path_.clone()));
        if (tokens_[*position_].kind() != TokenKind::SequenceEnd) {
            return Err(Error::invariant(path_.clone(), "sequence has unread elements"_str));
        }
        ++*position_;
        ended_ = true;
        return Ok(empty {});
    }
};

class TokenMapAccess {
    slice<Token> tokens_;
    usize*       position_;
    DataPath     path_;
    DataPath     value_path_;
    bool         pending_ {};
    bool         ended_ {};

public:
    TokenMapAccess(slice<Token> tokens, usize& position, DataPath path)
        : tokens_(tokens),
          position_(&position),
          path_(path.clone()),
          value_path_(rstd::move(path)) {}

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

    auto end() -> Result<empty, Error> {
        if (ended_) return Err(Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_) return Err(Error::invariant(path_.clone(), "map value is missing"_str));
        if (*position_ >= tokens_.len()) return Err(Error::unexpected_end(path_.clone()));
        if (tokens_[*position_].kind() != TokenKind::MapEnd) {
            return Err(Error::invariant(path_.clone(), "map has unread entries"_str));
        }
        ++*position_;
        ended_ = true;
        return Ok(empty {});
    }
};

inline auto TokenDeserializer::begin_sequence() -> Result<TokenSequenceAccess, Error> {
    auto start = take(TokenKind::SequenceStart, ValueKind::Sequence);
    if (start.is_err()) return Err(rstd::move(start).unwrap_err_unchecked());
    return Ok(TokenSequenceAccess(tokens_, *position_, path_.clone()));
}

inline auto TokenDeserializer::begin_map() -> Result<TokenMapAccess, Error> {
    auto start = take(TokenKind::MapStart, ValueKind::Map);
    if (start.is_err()) return Err(rstd::move(start).unwrap_err_unchecked());
    return Ok(TokenMapAccess(tokens_, *position_, path_.clone()));
}

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
