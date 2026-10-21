export module rstd.net.hostname;
export import rstd.io.error;
import rstd.alloc;

using namespace rstd::prelude;

export namespace rstd::net
{
// Returns the configured host name without resolving it through DNS or NSS.
auto hostname() -> io::Result<::alloc::string::String>;
} // namespace rstd::net
