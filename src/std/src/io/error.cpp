module rstd;
import :io.error;
import :sys.io;

namespace rstd::io::error
{

auto Error::last_os_error() noexcept -> Error {
    return from_raw_os_error(sys::io::last_os_error());
}

auto Error::kind() const noexcept -> ErrorKind {
    switch (tag()) {
    case Tag::Os: return sys::io::decode_error_kind(as_Os().code);
    case Tag::Kind: return as_Kind().kind;
    case Tag::Message: return as_Message().kind;
    }
    return ErrorKind { ErrorKind::Uncategorized };
}

} // namespace rstd::io::error
