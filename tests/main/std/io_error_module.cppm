export module rstd.tests.std_io_error_module;
import rstd.io.error;

using namespace rstd;
using namespace rstd::literals;

struct ErrorText {
    char   data[128] {};
    size_t len {};
};

auto error_text_write(void* context, const uint8_t* data, size_t size) -> bool {
    auto& out = *static_cast<ErrorText*>(context);
    if (size > sizeof(out.data) - out.len) return false;
    for (size_t i = 0; i < size; ++i) out.data[out.len++] = char(data[i]);
    return true;
}

template<typename Trait, typename T>
auto error_text_matches(T const& value, ref<str> expected) -> bool {
    ErrorText      text;
    fmt::Formatter formatter(&text, error_text_write);
    if (! as<Trait>(value).fmt(formatter) || text.len != expected.size().to_primitive())
        return false;
    for (size_t i = 0; i < text.len; ++i) {
        if (text.data[i] != static_cast<char>(expected.data()[i])) return false;
    }
    return true;
}

export auto std_io_error_module() -> bool {
    using io::error::Error;
    using io::error::ErrorKind;
    constexpr auto  message = Error::new_const(ErrorKind { ErrorKind::InvalidData }, "bad data");
    auto            kind    = ErrorKind { ErrorKind::NotFound };
    auto            error   = Error::from_kind(kind);
    io::Result<int> result  = Err(error);
    auto            native  = Error::from_raw_os_error(i32::MIN);
    return result.is_err() && result.unwrap_err().kind() == kind &&
           as<error::Error>(error).source().is_none() &&
           error_text_matches<fmt::Display>(kind, "entity not found"_str) &&
           error_text_matches<fmt::Debug>(kind, "entity not found"_str) &&
           error_text_matches<fmt::Display>(error, "entity not found"_str) &&
           error_text_matches<fmt::Debug>(error, "Kind(entity not found)"_str) &&
           error_text_matches<fmt::Display>(message, "bad data"_str) &&
           error_text_matches<fmt::Debug>(
               message, "Error { kind: invalid data, message: \"bad data\" }"_str) &&
           error_text_matches<fmt::Debug>(native, "Os(-2147483648)"_str);
}
