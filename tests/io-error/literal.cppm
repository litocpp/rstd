export module rstd.tests.io_error_literal;
import rstd.io.error;

using Error = rstd::io::error::Error;
using Kind  = rstd::io::error::ErrorKind;

constexpr auto static_message   = Error::make_const(Kind { Kind::InvalidData }, "bad data");
constexpr auto assigned_message = [] {
    auto value = Error::Kind(Kind { Kind::Other });
    value      = Error::make_const(Kind { Kind::InvalidData }, "bad data");
    return value;
}();
constexpr auto moved_message = [] {
    auto value = Error::from_static_message(rstd::io::error::Error_INVALID_UTF8);
    return rstd::move(value);
}();
static_assert(! __is_constructible(Error, const Error&));
static_assert(! __is_assignable(Error&, const Error&));
static_assert(static_message.is_Message());
static_assert(assigned_message.as_Message().message[0] == 'b');
static_assert(moved_message.as_Message().message[0] == 's');
static_assert(Error {}.as_Kind().kind.code == Kind::Uncategorized);
static_assert(rstd::io::error::Error_INVALID_UTF8.kind.code == Kind::InvalidData);
static_assert(rstd::io::error::Error_READ_EXACT_EOF.kind.code == Kind::UnexpectedEof);
static_assert(rstd::io::error::Error_WRITE_ALL_EOF.kind.code == Kind::WriteZero);

export auto io_error_literal_compatibility() -> bool {
    return moved_message.kind().code == Kind::InvalidData &&
           rstd::as<rstd::error::Error>(moved_message).source().is_none();
}
