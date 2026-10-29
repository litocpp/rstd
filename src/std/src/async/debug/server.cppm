export module rstd:async.debug.server;
export import :async.debug.protocol;
export import :net.tcp;
import :sync;
import :thread;
import :time;

using namespace rstd;
using ::alloc::vec::Vec;

namespace rstd::async::debug
{
export struct ServerOptions {
    net::SocketAddr address = net::SocketAddr::ipv4_loopback(u16());
    size_t          max_connections { 8 };
    size_t          max_message_bytes { 65536 };
    size_t          max_snapshot_bytes { 4 * 1024 * 1024 };
    size_t          page_records { 32 };
    time::Duration  idle_timeout      = time::Duration::from_secs(u64(30));
    time::Duration  snapshot_lifetime = time::Duration::from_secs(u64(30));
};
struct ServerFields {
    bool                                      stopped {};
    Option<rstd::io::Result<net::SocketAddr>> startup;
    Vec<Option<task::Waker>>                  waiters = Vec<Option<task::Waker>>::make();
};
struct ServerState {
    Handle                    debug;
    ServerOptions             options;
    sync::Mutex<ServerFields> fields;
    sync::Condvar             ready;
    ServerState(Handle handle, ServerOptions options)
        : debug(rstd::move(handle)), options(options), fields(ServerFields {}) {
        auto f = fields.lock().unwrap_unchecked();
        for (size_t i = 0; i <= options.max_connections; ++i) f->waiters.push(None<task::Waker>());
    }
    void stop();
};
export class Server {
    sync::Arc<ServerState>                              state_;
    Option<thread::JoinHandle<rstd::io::Result<empty>>> thread_;
    net::SocketAddr                                     address_;
    Server(sync::Arc<ServerState>, thread::JoinHandle<rstd::io::Result<empty>>, net::SocketAddr);

public:
    Server(const Server&)                    = delete;
    auto operator=(const Server&) -> Server& = delete;
    Server(Server&&) noexcept;
    auto operator=(Server&&) noexcept -> Server&;
    ~Server();
    static auto start(Handle handle, ServerOptions options = {}) -> rstd::io::Result<Server>;
    auto        local_addr() const noexcept -> net::SocketAddr { return address_; }
    void        stop() const;
    auto        join() -> rstd::io::Result<empty>;
};
} // namespace rstd::async::debug
