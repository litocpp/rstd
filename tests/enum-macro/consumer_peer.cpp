module rstd.tests.enum_macro.fixture;

import rstd;
import rstd.serde.token;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace enum_macro::fixture
{
auto message_from_peer() -> Message {
    auto value = Message::Named(i32(8), "peer"_Str);
    return rstd::as<rstd::clone::Clone>(value).clone();
}

auto failure_from_peer() -> Failure {
    return rstd::into<Failure>(Leaf { "peer"_Str });
}

auto decode_from_peer(ref<str> input) -> Result<Adjacent, rstd::serde::Error> {
    auto tokens = Vec<rstd::serde::Token>::make();
    tokens.push(rstd::serde::Token::container(rstd::serde::TokenKind::MapStart, usize(2)));
    tokens.push(rstd::serde::Token::string("kind"_str));
    tokens.push(rstd::serde::Token::string("Text"_str));
    tokens.push(rstd::serde::Token::string("data"_str));
    tokens.push(rstd::serde::Token::string(input));
    tokens.push(rstd::serde::Token::marker(rstd::serde::TokenKind::MapEnd));
    return rstd::serde::from_tokens<Adjacent>(tokens.as_slice());
}
} // namespace enum_macro::fixture
