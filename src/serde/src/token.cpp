module rstd.serde.token;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace rstd::serde
{

Token::Token(TokenKind kind): kind_(kind) {
}

auto Token::null() -> Token {
    return Token(TokenKind::Null);
}

auto Token::boolean(bool value) -> Token {
    auto token        = Token(TokenKind::Bool);
    token.bool_value_ = value;
    return token;
}

auto Token::signed_integer(i64 value) -> Token {
    auto token       = Token(TokenKind::I64);
    token.i64_value_ = value;
    return token;
}

auto Token::unsigned_integer(u64 value) -> Token {
    auto token       = Token(TokenKind::U64);
    token.u64_value_ = value;
    return token;
}

auto Token::floating(f64 value) -> Token {
    auto token       = Token(TokenKind::F64);
    token.f64_value_ = value;
    return token;
}

auto Token::string(ref<str> value) -> Token {
    auto token          = Token(TokenKind::String);
    token.string_value_ = String::make(value);
    return token;
}

auto Token::bytes(slice<u8> value) -> Token {
    auto token         = Token(TokenKind::Bytes);
    token.bytes_value_ = Vec<u8>::from(value);
    return token;
}

auto Token::marker(TokenKind kind) -> Token {
    return Token(kind);
}

auto Token::container(TokenKind kind, usize len) -> Token {
    auto token = Token(kind);
    token.len_ = len;
    return token;
}

auto Token::as_str() const noexcept -> ref<str> {
    return string_value_.as_str();
}

auto Token::as_bytes() const noexcept -> slice<u8> {
    return bytes_value_.as_slice();
}

auto Token::clone() const -> Token {
    auto token          = Token(kind_);
    token.bool_value_   = bool_value_;
    token.i64_value_    = i64_value_;
    token.u64_value_    = u64_value_;
    token.f64_value_    = f64_value_;
    token.len_          = len_;
    token.string_value_ = string_value_.clone();
    token.bytes_value_  = bytes_value_.clone();
    return token;
}

TokenSerializer::TokenSerializer(Vec<Token>& tokens): tokens_(&tokens) {
}

TokenSerializer::TokenSerializer(Vec<Token>& tokens, usize& open_compounds, DataPath path)
    : tokens_(&tokens), open_compounds_(&open_compounds), path_(rstd::move(path)) {
}

auto TokenSerializer::is_complete() const noexcept -> bool {
    return *open_compounds_ == usize();
}

auto TokenSerializer::serialize_none() -> result_type {
    tokens_->push(Token::null());
    return Ok(empty {});
}

auto TokenSerializer::serialize_unit() -> result_type {
    tokens_->push(Token::marker(TokenKind::Unit));
    return Ok(empty {});
}

auto TokenSerializer::serialize_bool(bool value) -> result_type {
    tokens_->push(Token::boolean(value));
    return Ok(empty {});
}

auto TokenSerializer::serialize_i64(i64 value) -> result_type {
    tokens_->push(Token::signed_integer(value));
    return Ok(empty {});
}

auto TokenSerializer::serialize_u64(u64 value) -> result_type {
    tokens_->push(Token::unsigned_integer(value));
    return Ok(empty {});
}

auto TokenSerializer::serialize_f64(f64 value) -> result_type {
    tokens_->push(Token::floating(value));
    return Ok(empty {});
}

auto TokenSerializer::serialize_string(ref<str> value) -> result_type {
    tokens_->push(Token::string(value));
    return Ok(empty {});
}

auto TokenSerializer::serialize_bytes(slice<u8> value) -> result_type {
    tokens_->push(Token::bytes(value));
    return Ok(empty {});
}

auto TokenSerializer::serialize_unit_variant(ref<str> variant) -> result_type {
    tokens_->push(Token::marker(TokenKind::EnumStart));
    tokens_->push(Token::string(variant));
    tokens_->push(Token::marker(TokenKind::Unit));
    tokens_->push(Token::marker(TokenKind::EnumEnd));
    return Ok(empty {});
}

auto TokenSerializer::begin_sequence(usize len) -> Result<TokenSequence, Error> {
    tokens_->push(Token::container(TokenKind::SequenceStart, len));
    ++*open_compounds_;
    return Ok(TokenSequence(*tokens_, *open_compounds_, path_.clone()));
}

auto TokenSerializer::begin_map(usize len) -> Result<TokenMap, Error> {
    tokens_->push(Token::container(TokenKind::MapStart, len));
    ++*open_compounds_;
    return Ok(TokenMap(*tokens_, *open_compounds_, path_.clone()));
}

auto TokenSerializer::invalid_value(ref<str> message) const -> Error {
    return Error::invalid_value(path_.clone(), message);
}

auto TokenSerializer::missing_field(ref<str> field) const -> Error {
    return Error::missing_field(path_.clone(), field);
}

auto TokenSerializer::unknown_field(ref<str> field) const -> Error {
    return Error::unknown_field(path_.clone(), field);
}

auto TokenSerializer::unknown_variant(ref<str> variant) const -> Error {
    return Error::unknown_variant(path_.clone(), variant);
}

auto TokenSerializer::duplicate_field(ref<str> field) const -> Error {
    return Error::duplicate_field(path_.clone(), field);
}

auto TokenSerializer::invariant(ref<str> message) const -> Error {
    return Error::invariant(path_.clone(), message);
}

auto TokenSerializer::unsupported(ref<str> message) const -> Error {
    return Error::unsupported(path_.clone(), message);
}

TokenSequence::TokenSequence(Vec<Token>& tokens, usize& open_compounds, DataPath path)
    : tokens_(&tokens), open_compounds_(&open_compounds), path_(rstd::move(path)) {
}

auto TokenSequence::end() -> Result<empty, Error> {
    if (ended_) return Err(Error::invariant(path_.clone(), "sequence already ended"_str));
    ended_ = true;
    --*open_compounds_;
    tokens_->push(Token::marker(TokenKind::SequenceEnd));
    return Ok(empty {});
}

TokenMap::TokenMap(Vec<Token>& tokens, usize& open_compounds, DataPath path)
    : tokens_(&tokens),
      open_compounds_(&open_compounds),
      path_(path.clone()),
      value_path_(rstd::move(path)) {
}

auto TokenMap::end() -> Result<empty, Error> {
    if (ended_) return Err(Error::invariant(path_.clone(), "map already ended"_str));
    if (pending_) return Err(Error::invariant(path_.clone(), "map value is missing"_str));
    ended_ = true;
    --*open_compounds_;
    tokens_->push(Token::marker(TokenKind::MapEnd));
    return Ok(empty {});
}

TokenDeserializer::TokenDeserializer(slice<Token> tokens,
                                     usize&       position,
                                     DataPath     path,
                                     bool         reject_unknown_fields)
    : tokens_(tokens),
      position_(&position),
      path_(rstd::move(path)),
      reject_unknown_fields_(reject_unknown_fields) {
}

TokenDeserializer::TokenDeserializer(slice<Token> tokens, DataPath path, bool reject_unknown_fields)
    : tokens_(tokens), path_(rstd::move(path)), reject_unknown_fields_(reject_unknown_fields) {
}

auto TokenDeserializer::peek() const noexcept -> Option<ref<Token>> {
    if (*position_ >= tokens_.len()) return None();
    return Some(ref<Token>::from_raw_parts(tokens_.as_raw_ptr() + position_->to_primitive()));
}

auto TokenDeserializer::take(TokenKind expected, ValueKind expected_kind)
    -> Result<ref<Token>, Error> {
    auto token = peek();
    if (token.is_none()) return Err(Error::unexpected_end(path_.clone()));
    if (token->get().kind() != expected) {
        return Err(
            Error::type_mismatch(path_.clone(), expected_kind, actual_kind(token->get().kind())));
    }
    ++*position_;
    return Ok(*token);
}

auto TokenDeserializer::actual_kind(TokenKind kind) noexcept -> ValueKind {
    switch (kind) {
    case TokenKind::Null: return ValueKind::Null;
    case TokenKind::Unit: return ValueKind::Unit;
    case TokenKind::Bool: return ValueKind::Boolean;
    case TokenKind::I64: return ValueKind::SignedInteger;
    case TokenKind::U64: return ValueKind::UnsignedInteger;
    case TokenKind::F64: return ValueKind::Float;
    case TokenKind::String: return ValueKind::String;
    case TokenKind::Bytes: return ValueKind::Bytes;
    case TokenKind::SomeStart:
    case TokenKind::SomeEnd: return ValueKind::Enum;
    case TokenKind::SequenceStart:
    case TokenKind::SequenceEnd: return ValueKind::Sequence;
    case TokenKind::MapStart:
    case TokenKind::MapEnd: return ValueKind::Map;
    case TokenKind::EnumStart:
    case TokenKind::EnumEnd: return ValueKind::Enum;
    }
    rstd::unreachable();
}

TokenDeserializer::TokenDeserializer(slice<Token> tokens): tokens_(tokens) {
}

auto TokenDeserializer::position() const noexcept -> usize {
    return *position_;
}

auto TokenDeserializer::path() const noexcept -> const DataPath& {
    return path_;
}

auto TokenDeserializer::invalid_value(ref<str> message) const -> Error {
    return Error::invalid_value(path_.clone(), message);
}

auto TokenDeserializer::missing_field(ref<str> field) const -> Error {
    return Error::missing_field(path_.clone(), field);
}

auto TokenDeserializer::unknown_field(ref<str> field) const -> Error {
    return Error::unknown_field(path_.clone(), field);
}

auto TokenDeserializer::unknown_variant(ref<str> variant) const -> Error {
    return Error::unknown_variant(path_.clone(), variant);
}

auto TokenDeserializer::duplicate_field(ref<str> field) const -> Error {
    return Error::duplicate_field(path_.clone(), field);
}

auto TokenDeserializer::invariant(ref<str> message) const -> Error {
    return Error::invariant(path_.clone(), message);
}

auto TokenDeserializer::unsupported(ref<str> message) const -> Error {
    return Error::unsupported(path_.clone(), message);
}

auto TokenDeserializer::deserialize_bool() -> Result<bool, Error> {
    auto token = take(TokenKind::Bool, ValueKind::Boolean);
    if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
    return Ok(token->get().as_bool());
}

auto TokenDeserializer::deserialize_unit() -> Result<empty, Error> {
    auto token = take(TokenKind::Unit, ValueKind::Unit);
    if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
    return Ok(empty {});
}

auto TokenDeserializer::deserialize_i64() -> Result<i64, Error> {
    auto token = take(TokenKind::I64, ValueKind::SignedInteger);
    if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
    return Ok(token->get().as_i64());
}

auto TokenDeserializer::deserialize_u64() -> Result<u64, Error> {
    auto token = take(TokenKind::U64, ValueKind::UnsignedInteger);
    if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
    return Ok(token->get().as_u64());
}

auto TokenDeserializer::deserialize_f64() -> Result<f64, Error> {
    auto token = take(TokenKind::F64, ValueKind::Float);
    if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
    return Ok(token->get().as_f64());
}

auto TokenDeserializer::deserialize_string() -> Result<String, Error> {
    auto token = take(TokenKind::String, ValueKind::String);
    if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
    return Ok(String::make(token->get().as_str()));
}

auto TokenDeserializer::deserialize_bytes() -> Result<Vec<u8>, Error> {
    auto token = take(TokenKind::Bytes, ValueKind::Bytes);
    if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
    return Ok(Vec<u8>::from(token->get().as_bytes()));
}

TokenSequenceAccess::TokenSequenceAccess(slice<Token> tokens, usize& position, DataPath path)
    : tokens_(tokens), position_(&position), path_(rstd::move(path)) {
}

auto TokenSequenceAccess::end() -> Result<empty, Error> {
    if (ended_) return Err(Error::invariant(path_.clone(), "sequence already ended"_str));
    if (*position_ >= tokens_.len()) return Err(Error::unexpected_end(path_.clone()));
    if (tokens_[*position_].kind() != TokenKind::SequenceEnd) {
        return Err(Error::invariant(path_.clone(), "sequence has unread elements"_str));
    }
    ++*position_;
    ended_ = true;
    return Ok(empty {});
}

TokenMapAccess::TokenMapAccess(slice<Token> tokens, usize& position, DataPath path)
    : tokens_(tokens), position_(&position), path_(path.clone()), value_path_(rstd::move(path)) {
}

auto TokenMapAccess::ignore_value() -> Result<empty, Error> {
    if (ended_) return Err(Error::invariant(path_.clone(), "map already ended"_str));
    if (! pending_) return Err(Error::invariant(path_.clone(), "map key is missing"_str));
    auto child  = TokenDeserializer(tokens_, *position_, value_path_.clone());
    auto result = child.ignore_value();
    if (result.is_err()) return result;
    pending_ = false;
    return Ok(empty {});
}

auto TokenMapAccess::end() -> Result<empty, Error> {
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

TokenEnumAccess::TokenEnumAccess(slice<Token> tokens,
                                 usize&       position,
                                 DataPath     path,
                                 String       variant)
    : tokens_(tokens),
      position_(&position),
      path_(rstd::move(path)),
      variant_(rstd::move(variant)) {
}

auto TokenEnumAccess::finish() -> Result<empty, Error> {
    auto child = TokenDeserializer(tokens_, *position_, path_.with_variant(variant_.as_str()));
    auto end   = child.take(TokenKind::EnumEnd, ValueKind::Enum);
    if (end.is_err()) return Err(rstd::move(end).unwrap_err_unchecked());
    consumed_ = true;
    return Ok(empty {});
}

auto TokenEnumAccess::variant() const noexcept -> ref<str> {
    return variant_.as_str();
}

auto TokenEnumAccess::unit() -> Result<empty, Error> {
    if (consumed_) return Err(Error::invariant(path_.clone(), "enum already consumed"_str));
    auto child  = TokenDeserializer(tokens_, *position_, path_.with_variant(variant_.as_str()));
    auto result = child.deserialize_unit();
    if (result.is_err()) return result;
    return finish();
}

auto TokenDeserializer::begin_sequence() -> Result<TokenSequenceAccess, Error> {
    auto start = take(TokenKind::SequenceStart, ValueKind::Sequence);
    if (start.is_err()) return Err(rstd::move(start).unwrap_err_unchecked());
    return Ok(TokenSequenceAccess(tokens_, *position_, path_.clone()));
}

auto TokenDeserializer::begin_map() -> Result<TokenMapAccess, Error> {
    auto start = take(TokenKind::MapStart, ValueKind::Map);
    if (start.is_err()) return Err(rstd::move(start).unwrap_err_unchecked());
    return Ok(TokenMapAccess(tokens_, *position_, path_.clone()));
}

auto TokenDeserializer::begin_enum() -> Result<TokenEnumAccess, Error> {
    auto start = take(TokenKind::EnumStart, ValueKind::Enum);
    if (start.is_err()) return Err(rstd::move(start).unwrap_err_unchecked());
    auto variant = take(TokenKind::String, ValueKind::String);
    if (variant.is_err()) return Err(rstd::move(variant).unwrap_err_unchecked());
    return Ok(
        TokenEnumAccess(tokens_, *position_, path_.clone(), String::make(variant->get().as_str())));
}

auto TokenDeserializer::begin_internal_enum(ref<str> tag, UnknownFieldPolicy unknown)
    -> Result<TokenTaggedEnumAccess, Error> {
    auto started = begin_map();
    if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
    auto map     = rstd::move(started).unwrap_unchecked();
    auto payload = Vec<Token>::make();
    payload.push(Token::container(TokenKind::MapStart, usize()));
    auto           fields = usize();
    Option<String> variant;
    for (;;) {
        auto key = map.next_key<String>();
        if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
        if (key->is_none()) break;
        auto name = rstd::move(*key).unwrap_unchecked();
        if (name.as_str() == tag) {
            if (variant.is_some()) return Err(duplicate_field(tag));
            auto value = map.next_value<String>();
            if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
            variant = Some(rstd::move(value).unwrap_unchecked());
        } else {
            ++fields;
            auto first   = *position_;
            auto ignored = map.ignore_value();
            if (ignored.is_err()) return Err(rstd::move(ignored).unwrap_err_unchecked());
            payload.push(Token::string(name.as_str()));
            for (auto index = first; index < *position_; ++index)
                payload.push(tokens_[index].clone());
        }
    }
    auto ended = map.end();
    if (ended.is_err()) return Err(rstd::move(ended).unwrap_err_unchecked());
    if (variant.is_none()) return Err(missing_field(tag));
    payload[usize()] = Token::container(TokenKind::MapStart, fields);
    payload.push(Token::marker(TokenKind::MapEnd));
    return Ok(
        TokenTaggedEnumAccess(rstd::move(*variant),
                              TokenTaggedPayload(Some(rstd::move(payload)), path_.clone(), None()),
                              unknown));
}

auto TokenDeserializer::begin_adjacent_enum(ref<str>           tag,
                                            ref<str>           content,
                                            UnknownFieldPolicy unknown)
    -> Result<TokenTaggedEnumAccess, Error> {
    if (tag == content) return Err(invariant("adjacent tag and content must differ"_str));
    auto started = begin_map();
    if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
    auto               map = rstd::move(started).unwrap_unchecked();
    Option<String>     variant;
    Option<Vec<Token>> payload;
    for (;;) {
        auto key = map.next_key<String>();
        if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
        if (key->is_none()) break;
        auto name = rstd::move(*key).unwrap_unchecked();
        if (name.as_str() == tag) {
            if (variant.is_some()) return Err(duplicate_field(tag));
            auto value = map.next_value<String>();
            if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
            variant = Some(rstd::move(value).unwrap_unchecked());
        } else if (name.as_str() == content) {
            if (payload.is_some()) return Err(duplicate_field(content));
            auto first   = *position_;
            auto ignored = map.ignore_value();
            if (ignored.is_err()) return Err(rstd::move(ignored).unwrap_err_unchecked());
            auto tokens = Vec<Token>::with_capacity(*position_ - first);
            for (auto index = first; index < *position_; ++index)
                tokens.push(tokens_[index].clone());
            payload = Some(rstd::move(tokens));
        } else {
            if (unknown == UnknownFieldPolicy::Reject) return Err(unknown_field(name.as_str()));
            auto ignored = map.ignore_value();
            if (ignored.is_err()) return Err(rstd::move(ignored).unwrap_err_unchecked());
        }
    }
    auto ended = map.end();
    if (ended.is_err()) return Err(rstd::move(ended).unwrap_err_unchecked());
    if (variant.is_none()) return Err(missing_field(tag));
    return Ok(TokenTaggedEnumAccess(
        rstd::move(*variant),
        TokenTaggedPayload(rstd::move(payload), path_.clone(), Some(String::make(content))),
        unknown));
}

TokenTaggedPayload::TokenTaggedPayload(Option<Vec<Token>> payload,
                                       DataPath           path,
                                       Option<String>     content)
    : payload_(rstd::move(payload)), path_(rstd::move(path)), content_(rstd::move(content)) {
}

auto TokenTaggedPayload::deserializer(UnknownFieldPolicy unknown) -> TokenDeserializer {
    auto path = content_.is_some() ? path_.with_field(content_->as_str()) : path_.clone();
    return TokenDeserializer(payload_->as_slice(),
                             rstd::move(path),
                             is_internal() && unknown == UnknownFieldPolicy::Reject);
}

auto TokenTaggedPayload::unit(TokenDeserializer& source) -> Result<empty, Error> {
    if (payload_->len() == usize(1) && (*payload_)[usize()].kind() == TokenKind::Null)
        return source.ignore_value();
    return source.deserialize_unit();
}

auto TokenTaggedPayload::end(const TokenDeserializer& source) const -> Result<empty, Error> {
    if (source.position() != payload_->len())
        return Err(source.invariant("trailing enum payload tokens"_str));
    return Ok(empty {});
}

auto TokenTaggedPayload::missing_value() const -> Error {
    return Error::missing_field(path_.clone(), content_->as_str());
}

auto TokenTaggedPayload::invariant(ref<str> message) const -> Error {
    return Error::invariant(path_.clone(), message);
}

auto TokenDeserializer::ignore_value() -> Result<empty, Error> {
    auto token = peek();
    if (token.is_none()) return Err(Error::unexpected_end(path_.clone()));
    switch (token->get().kind()) {
    case TokenKind::Null:
    case TokenKind::Unit:
    case TokenKind::Bool:
    case TokenKind::I64:
    case TokenKind::U64:
    case TokenKind::F64:
    case TokenKind::String:
    case TokenKind::Bytes: ++*position_; return Ok(empty {});
    case TokenKind::SomeStart: {
        ++*position_;
        auto value = ignore_value();
        if (value.is_err()) return value;
        auto end = take(TokenKind::SomeEnd, ValueKind::Enum);
        if (end.is_err()) return Err(rstd::move(end).unwrap_err_unchecked());
        return Ok(empty {});
    }
    case TokenKind::SequenceStart: {
        ++*position_;
        while (*position_ < tokens_.len() && tokens_[*position_].kind() != TokenKind::SequenceEnd) {
            auto value = ignore_value();
            if (value.is_err()) return value;
        }
        auto end = take(TokenKind::SequenceEnd, ValueKind::Sequence);
        if (end.is_err()) return Err(rstd::move(end).unwrap_err_unchecked());
        return Ok(empty {});
    }
    case TokenKind::MapStart: {
        ++*position_;
        while (*position_ < tokens_.len() && tokens_[*position_].kind() != TokenKind::MapEnd) {
            auto key = ignore_value();
            if (key.is_err()) return key;
            auto value = ignore_value();
            if (value.is_err()) return value;
        }
        auto end = take(TokenKind::MapEnd, ValueKind::Map);
        if (end.is_err()) return Err(rstd::move(end).unwrap_err_unchecked());
        return Ok(empty {});
    }
    case TokenKind::EnumStart: {
        ++*position_;
        auto variant = take(TokenKind::String, ValueKind::String);
        if (variant.is_err()) return Err(rstd::move(variant).unwrap_err_unchecked());
        auto value = ignore_value();
        if (value.is_err()) return value;
        auto end = take(TokenKind::EnumEnd, ValueKind::Enum);
        if (end.is_err()) return Err(rstd::move(end).unwrap_err_unchecked());
        return Ok(empty {});
    }
    case TokenKind::SomeEnd:
    case TokenKind::SequenceEnd:
    case TokenKind::MapEnd:
    case TokenKind::EnumEnd:
        return Err(Error::invariant(path_.clone(), "unexpected compound end token"_str));
    }
    rstd::unreachable();
}

static_assert(Serializer<TokenSerializer>);
static_assert(Deserializer<TokenDeserializer>);
static_assert(EnumAccess<TokenTaggedEnumAccess>);
static_assert(InternalVariantDeserializer<TokenDeserializer>);
static_assert(AdjacentVariantDeserializer<TokenDeserializer>);
static_assert(Serializer<InternalTagSerializer<TokenSerializer>>);

} // namespace rstd::serde
