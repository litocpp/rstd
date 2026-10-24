export module rstd.net.lookup_host;
export import rstd.net.socket_addr;
export import rstd.io.error;
import rstd.alloc;
using namespace rstd::prelude;
export namespace rstd::net
{
struct LookupError {
    i32                      code;
    ::alloc::string::String  message;
    Option<io::error::Error> cause;
    auto clone() const -> LookupError { return { code, message.clone(), cause }; }
};
// Numeric IPs are parsed directly; other names use the operating system name service.
// Blocks the calling thread and provides no timeout or cancellation.
// Returns distinct addresses in system order with the supplied port. Empty results are errors.
// Empty names and interior NULs are rejected; code is zero for errors without a resolver code.
auto lookup_host(ref<str> host, u16 port) -> Result<::alloc::vec::Vec<SocketAddr>, LookupError>;
} // namespace rstd::net
export namespace rstd
{
template<>
struct Impl<fmt::Display, net::LookupError> : ImplBase<net::LookupError> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_fmt(
            fmt::Arguments::make("name lookup: {} ({})", this->self().message, this->self().code));
    }
};
template<>
struct Impl<fmt::Debug, net::LookupError> : Impl<fmt::Display, net::LookupError> {};
template<>
struct Impl<error::Error, net::LookupError> : DefaultInImpl<error::Error, net::LookupError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        if (this->self().cause.is_some())
            return Some(dyn<error::Error>::from_ref(*this->self().cause));
        return None();
    }
};
} // namespace rstd
