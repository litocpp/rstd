module;

#include <rstd/macro.hpp>

#if RSTD_OS_LINUX
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

module rstd_benches;
import rstd.bench;
import rstd;
import :async.support;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::vec::Vec;

struct LoopbackStreams {
    net::TcpStream  client;
    net::TcpStream  server;
    bytes::BytesMut server_received;
    bytes::BytesMut client_received;

    LoopbackStreams(net::TcpStream client, net::TcpStream server, rstd::size_t payload_len)
        : client(rstd::move(client)),
          server(rstd::move(server)),
          server_received(bytes::BytesMut::with_capacity(usize(payload_len))),
          client_received(bytes::BytesMut::with_capacity(usize(payload_len))) {}
};

auto loopback_payload(rstd::size_t payload_len) -> bytes::Bytes {
    auto payload = Vec<u8>::with_capacity(usize(payload_len));
    payload.resize(usize(payload_len), u8::from_byte(byte { 'p' }));
    return bytes::Bytes::from_vec(rstd::move(payload));
}

auto loopback_run_config(rstd::size_t concurrency, rstd::size_t payload_len) -> bench::RunConfig {
    auto const roundtrips = LOOPBACK_BATCH * concurrency;
    return bench::RunConfig {
        .batch               = f64(static_cast<double>(roundtrips)),
        .items_per_iteration = u64(roundtrips * 4),
        .bytes_per_iteration = u64(roundtrips * payload_len * 2),
    };
}

auto invalid_loopback_data() -> io::error::Error {
    return io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::InvalidData });
}

async::coro<io::Result<LoopbackStreams>> open_loopback_streams(net::TcpListener& listener,
                                                               net::SocketAddr   address,
                                                               rstd::size_t      payload_len) {
    auto connected = co_await net::TcpStream::connect(address);
    if (connected.is_err()) co_return Err(rstd::move(connected).unwrap_err_unchecked());

    auto accepted = co_await listener.accept();
    if (accepted.is_err()) co_return Err(rstd::move(accepted).unwrap_err_unchecked());

    auto client         = rstd::move(connected).unwrap_unchecked();
    auto pair           = rstd::move(accepted).unwrap_unchecked();
    auto server         = rstd::move(pair.template get<0>());
    auto client_nodelay = client.set_nodelay(true);
    if (client_nodelay.is_err()) {
        co_return Err(rstd::move(client_nodelay).unwrap_err_unchecked());
    }
    auto server_nodelay = server.set_nodelay(true);
    if (server_nodelay.is_err()) {
        co_return Err(rstd::move(server_nodelay).unwrap_err_unchecked());
    }
    co_return Ok(LoopbackStreams { rstd::move(client), rstd::move(server), payload_len });
}

async::coro<io::Result<empty>>
loopback_ping_pong(LoopbackStreams& streams, const bytes::Bytes& payload, usize count) {
    for (usize index {}; index < count; ++index) {
        streams.server_received.clear();
        auto client_write = co_await async::io::write_all(streams.client, payload);
        if (client_write.is_err()) {
            co_return Err(rstd::move(client_write).unwrap_err_unchecked());
        }

        auto server_read =
            co_await async::io::read_exact(streams.server, streams.server_received, payload.len());
        if (server_read.is_err()) {
            co_return Err(rstd::move(server_read).unwrap_err_unchecked());
        }
        if (streams.server_received.len() != payload.len() ||
            streams.server_received[usize()] != payload[usize()] ||
            streams.server_received[payload.len() - usize(1)] !=
                payload[payload.len() - usize(1)]) {
            co_return Err(invalid_loopback_data());
        }

        streams.client_received.clear();
        auto server_write = co_await async::io::write_all(streams.server, payload);
        if (server_write.is_err()) {
            co_return Err(rstd::move(server_write).unwrap_err_unchecked());
        }

        auto client_read =
            co_await async::io::read_exact(streams.client, streams.client_received, payload.len());
        if (client_read.is_err()) {
            co_return Err(rstd::move(client_read).unwrap_err_unchecked());
        }
        if (streams.client_received.len() != payload.len() ||
            streams.client_received[usize()] != payload[usize()] ||
            streams.client_received[payload.len() - usize(1)] !=
                payload[payload.len() - usize(1)]) {
            co_return Err(invalid_loopback_data());
        }
    }
    co_return Ok(empty {});
}

async::coro<io::Result<empty>> loopback_ping_pong_concurrent(Vec<LoopbackStreams>& streams,
                                                             const bytes::Bytes&   payload,
                                                             usize                 count) {
    auto handles = Vec<async::JoinHandle<io::Result<empty>>>::with_capacity(streams.len());
    for (usize index {}; index < streams.len(); ++index) {
        handles.push(async::spawn_local(loopback_ping_pong(streams[index], payload, count)));
    }

    auto joined = co_await async::join_all(rstd::move(handles));
    for (usize index {}; index < joined.len(); ++index) {
        auto& joined_result = joined[index];
        if (joined_result.is_err()) co_return Err(invalid_loopback_data());
        auto result = rstd::move(joined_result).unwrap_unchecked();
        if (result.is_err()) co_return Err(rstd::move(result).unwrap_err_unchecked());
    }
    co_return Ok(empty {});
}

auto run_loopback_ping_pong_thread_pool(async::Runtime&       runtime,
                                        Vec<LoopbackStreams>& streams,
                                        const bytes::Bytes&   payload,
                                        usize                 count) -> io::Result<empty> {
    auto handles = Vec<async::JoinHandle<io::Result<empty>>>::with_capacity(streams.len());
    for (usize index {}; index < streams.len(); ++index) {
        handles.push(runtime.spawn(loopback_ping_pong(streams[index], payload, count)));
    }

    auto joined = runtime.block_on(async::join_all(rstd::move(handles)));
    for (usize index {}; index < joined.len(); ++index) {
        auto& joined_result = joined[index];
        if (joined_result.is_err()) return Err(invalid_loopback_data());
        auto result = rstd::move(joined_result).unwrap_unchecked();
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
    }
    return Ok(empty {});
}

#if RSTD_OS_LINUX
struct SyncLoopbackStreams {
    os::socket::OwnedSocket client;
    os::socket::OwnedSocket server;
    Vec<u8>                 server_received;
    Vec<u8>                 client_received;

    SyncLoopbackStreams(os::socket::OwnedSocket client,
                        os::socket::OwnedSocket server,
                        rstd::size_t            payload_len)
        : client(rstd::move(client)),
          server(rstd::move(server)),
          server_received(Vec<u8>::with_capacity(usize(payload_len))),
          client_received(Vec<u8>::with_capacity(usize(payload_len))) {
        server_received.resize(usize(payload_len), u8());
        client_received.resize(usize(payload_len), u8());
    }
};

auto set_sync_nodelay(os::socket::RawSocket socket) -> io::Result<empty> {
    int enabled = 1;
    if (::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) != 0) {
        return Err(io::error::Error::last_os_error());
    }
    return Ok(empty {});
}

auto open_sync_loopback_streams(rstd::size_t payload_len) -> io::Result<SyncLoopbackStreams> {
    auto listener_raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_raw < 0) return Err(io::error::Error::last_os_error());
    auto listener = os::socket::OwnedSocket::from_raw_socket(listener_raw);

    auto address = sockaddr_in {
        .sin_family = AF_INET,
        .sin_port   = 0,
        .sin_addr   = { .s_addr = ::htonl(INADDR_LOOPBACK) },
    };
    if (::bind(listener.as_raw_socket(),
               reinterpret_cast<const sockaddr*>(rstd::addressof(address)),
               sizeof(address)) != 0 ||
        ::listen(listener.as_raw_socket(), 1) != 0) {
        return Err(io::error::Error::last_os_error());
    }

    auto address_len = static_cast<socklen_t>(sizeof(address));
    if (::getsockname(listener.as_raw_socket(),
                      reinterpret_cast<sockaddr*>(rstd::addressof(address)),
                      &address_len) != 0) {
        return Err(io::error::Error::last_os_error());
    }

    auto client_raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client_raw < 0) return Err(io::error::Error::last_os_error());
    auto client = os::socket::OwnedSocket::from_raw_socket(client_raw);
    if (::connect(client.as_raw_socket(),
                  reinterpret_cast<const sockaddr*>(rstd::addressof(address)),
                  address_len) != 0) {
        return Err(io::error::Error::last_os_error());
    }

    int server_raw;
    do {
        server_raw = ::accept(listener.as_raw_socket(), nullptr, nullptr);
    } while (server_raw < 0 && errno == EINTR);
    if (server_raw < 0) return Err(io::error::Error::last_os_error());
    auto server = os::socket::OwnedSocket::from_raw_socket(server_raw);

    auto client_nodelay = set_sync_nodelay(client.as_raw_socket());
    if (client_nodelay.is_err()) return Err(rstd::move(client_nodelay).unwrap_err_unchecked());
    auto server_nodelay = set_sync_nodelay(server.as_raw_socket());
    if (server_nodelay.is_err()) return Err(rstd::move(server_nodelay).unwrap_err_unchecked());
    return Ok(SyncLoopbackStreams { rstd::move(client), rstd::move(server), payload_len });
}

auto sync_send_exact(os::socket::RawSocket socket, const byte* data, rstd::size_t len) -> bool {
    rstd::size_t sent {};
    while (sent < len) {
        auto result = ::send(socket, data + sent, len - sent, MSG_NOSIGNAL);
        if (result > 0) {
            sent += static_cast<rstd::size_t>(result);
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

auto sync_recv_exact(os::socket::RawSocket socket, byte* data, rstd::size_t len) -> bool {
    rstd::size_t received {};
    while (received < len) {
        auto result = ::recv(socket, data + received, len - received, 0);
        if (result > 0) {
            received += static_cast<rstd::size_t>(result);
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

auto sync_loopback_ping_pong(SyncLoopbackStreams& streams, const bytes::Bytes& payload, usize count)
    -> bool {
    for (usize index {}; index < count; ++index) {
        if (! sync_send_exact(
                streams.client.as_raw_socket(), payload.data(), payload.len().to_primitive()) ||
            ! sync_recv_exact(streams.server.as_raw_socket(),
                              streams.server_received.data(),
                              streams.server_received.len().to_primitive()) ||
            streams.server_received[usize()] != payload[usize()] ||
            streams.server_received[payload.len() - usize(1)] !=
                payload[payload.len() - usize(1)] ||
            ! sync_send_exact(
                streams.server.as_raw_socket(), payload.data(), payload.len().to_primitive()) ||
            ! sync_recv_exact(streams.client.as_raw_socket(),
                              streams.client_received.data(),
                              streams.client_received.len().to_primitive()) ||
            streams.client_received[usize()] != payload[usize()] ||
            streams.client_received[payload.len() - usize(1)] !=
                payload[payload.len() - usize(1)]) {
            return false;
        }
    }
    return true;
}

struct SyncConcurrentFields {
    rstd::uint64_t generation {};
    usize          count {};
    usize          completed {};
    bool           stop {};
    bool           valid { true };
};

struct SyncConcurrentState {
    sync::Mutex<SyncConcurrentFields> fields;
    sync::Condvar                     changed;

    SyncConcurrentState(): fields(SyncConcurrentFields {}), changed() {}
};

struct SyncWorkers {
    sync::Arc<SyncConcurrentState> state;
    Vec<thread::JoinHandle<bool>>  handles;

    auto finish() -> Result<empty, String> {
        {
            auto guard  = state->fields.lock().unwrap();
            guard->stop = true;
            state->changed.notify_all();
        }
        auto failures = usize();
        for (usize i; i < handles.len(); ++i) {
            auto result = rstd::move(handles[i]).join();
            if (result.is_err() || ! *result) ++failures;
        }
        handles.clear();
        if (failures != usize())
            return Err(rstd::format("{} loopback workers failed to join", failures));
        return Ok(empty {});
    }
    ~SyncWorkers() { (void)finish(); }
};

auto sync_loopback_worker(sync::Arc<SyncConcurrentState> state,
                          SyncLoopbackStreams            streams,
                          rstd::size_t                   payload_len) -> bool {
    auto           payload    = loopback_payload(payload_len);
    rstd::uint64_t generation = 0;
    while (true) {
        usize count;
        {
            auto guard = state->fields.lock().unwrap_unchecked();
            state->changed.wait_while(guard, [&](const SyncConcurrentFields& fields) {
                return ! fields.stop && fields.generation == generation;
            });
            if (guard->stop) return true;
            generation = guard->generation;
            count      = guard->count;
        }

        auto const succeeded = sync_loopback_ping_pong(streams, payload, count);
        auto       guard     = state->fields.lock().unwrap_unchecked();
        guard->valid         = guard->valid && succeeded;
        ++guard->completed;
        if (guard->completed == usize(LOOPBACK_CONCURRENCY)) state->changed.notify_one();
    }
}

auto run_sync_loopback_concurrent(const sync::Arc<SyncConcurrentState>& state, usize count)
    -> bool {
    auto guard       = state->fields.lock().unwrap_unchecked();
    guard->count     = count;
    guard->completed = usize();
    ++guard->generation;
    state->changed.notify_all();
    state->changed.wait_while(guard, [](const SyncConcurrentFields& fields) {
        return fields.completed != usize(LOOPBACK_CONCURRENCY);
    });
    return guard->valid;
}

#endif

#if RSTD_OS_LINUX
auto io_loopback_ping_pong_sync(bench::BenchConfig config,
                                rstd::size_t       payload_len,
                                const char*        name) -> rstd_bench::CaseRunResult {
    auto opened = open_sync_loopback_streams(payload_len);
    if (opened.is_err())
        return rstd_bench::failed(rstd::format("connection failed: {}", opened.unwrap_err()));
    auto streams = rstd::move(opened).ok();
    auto payload = loopback_payload(payload_len);
    bool valid   = streams.is_some() && sync_loopback_ping_pong(*streams, payload, usize(1));

    auto operation_error = Option<String> {};
    auto calls           = rstd::uint64_t {};
    auto roundtrips      = rstd::uint64_t {};
    auto run_config      = loopback_run_config(1, payload_len);
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            if (! valid) {
                rstd::hint::black_box(valid);
                return;
            }
            valid = sync_loopback_ping_pong(*streams, payload, usize(LOOPBACK_BATCH));
            if (! valid) return;
            ++calls;
            roundtrips += LOOPBACK_BATCH;
            rstd::hint::black_box(roundtrips);
        },
        [&]() -> Result<empty, String> {
            if (operation_error.is_some()) return Err(rstd::move(*operation_error));
            if (! (valid && calls != 0 && roundtrips == calls * LOOPBACK_BATCH))
                return Err(String::make(rstd_bench::text("validation failed")));
            return Ok(empty {});
        });
}

auto io_loopback_ping_pong_sync_4way(bench::BenchConfig config,
                                     rstd::size_t       payload_len,
                                     const char*        name) -> rstd_bench::CaseRunResult {
    auto state = sync::Arc<SyncConcurrentState>::make();
    auto workers =
        SyncWorkers { state.clone(),
                      Vec<thread::JoinHandle<bool>>::with_capacity(usize(LOOPBACK_CONCURRENCY)) };
    auto& handles = workers.handles;
    bool  valid   = true;

    for (rstd::size_t index = 0; index < LOOPBACK_CONCURRENCY; ++index) {
        auto opened = open_sync_loopback_streams(payload_len);
        if (opened.is_err())
            return rstd_bench::failed(rstd::format("connection failed: {}", opened.unwrap_err()),
                                      workers.finish());
        auto worker_state = state.clone();
        auto spawned      = thread::spawn([state   = rstd::move(worker_state),
                                           streams = rstd::move(opened).unwrap_unchecked(),
                                           payload_len]() mutable {
            return sync_loopback_worker(rstd::move(state), rstd::move(streams), payload_len);
        });
        if (spawned.is_err())
            return rstd_bench::failed(rstd::format("spawn failed: {}", spawned.unwrap_err()),
                                      workers.finish());
        handles.push(rstd::move(spawned).unwrap_unchecked());
    }
    if (! run_sync_loopback_concurrent(state, usize(1)))
        return rstd_bench::failed("loopback priming failed"_Str, workers.finish());

    auto operation_error = Option<String> {};
    auto calls           = rstd::uint64_t {};
    auto roundtrips      = rstd::uint64_t {};
    auto run_config      = loopback_run_config(LOOPBACK_CONCURRENCY, payload_len);
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            if (! valid) {
                rstd::hint::black_box(valid);
                return;
            }
            valid = run_sync_loopback_concurrent(state, usize(LOOPBACK_BATCH));
            if (! valid) return;
            ++calls;
            roundtrips += LOOPBACK_BATCH * LOOPBACK_CONCURRENCY;
            rstd::hint::black_box(roundtrips);
        },
        [&]() -> Result<empty, String> {
            if (operation_error.is_some()) return Err(rstd::move(*operation_error));
            if (! (valid && calls != 0 &&
                   roundtrips == calls * LOOPBACK_BATCH * LOOPBACK_CONCURRENCY))
                return Err(String::make(rstd_bench::text("validation failed")));
            return Ok(empty {});
        },
        [&] {
            return workers.finish();
        });
}

template<rstd::size_t PayloadLen>
auto io_loopback_ping_pong_sync_case(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    return io_loopback_ping_pong_sync(rstd::move(config), PayloadLen, name);
}

template<rstd::size_t PayloadLen>
auto io_loopback_ping_pong_sync_4way_case(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    return io_loopback_ping_pong_sync_4way(rstd::move(config), PayloadLen, name);
}

#endif

auto io_loopback_ping_pong(bench::BenchConfig config,
                           rstd::size_t       payload_len,
                           const char*        name,
                           IoBackend backend = IoBackend::Auto) -> rstd_bench::CaseRunResult {
    auto runtime = make_io_runtime(backend);
    if (runtime.is_err()) return runtime_failure(runtime.unwrap_err());
    auto listener = Option<net::TcpListener> {};
    auto streams  = Option<LoopbackStreams> {};
    auto payload  = loopback_payload(payload_len);
    bool valid    = true;

    if (valid) {
        auto bound = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
        if (bound.is_err()) {
            return rstd_bench::failed(rstd::format("bind failed: {}", bound.unwrap_err()));
        } else {
            listener = Some(rstd::move(bound).unwrap_unchecked());
        }
    }
    if (valid) {
        auto address = listener->local_addr();
        if (address.is_err()) {
            return rstd_bench::failed(rstd::format("address failed: {}", address.unwrap_err()));
        } else {
            auto opened = runtime->block_on(open_loopback_streams(
                *listener, rstd::move(address).unwrap_unchecked(), payload_len));
            if (opened.is_err()) {
                return rstd_bench::failed(
                    rstd::format("connection failed: {}", opened.unwrap_err()));
            } else {
                streams = Some(rstd::move(opened).unwrap_unchecked());
            }
        }
    }
    if (valid) {
        auto primed = runtime->block_on(loopback_ping_pong(*streams, payload, usize(1)));
        if (primed.is_err())
            return rstd_bench::failed(rstd::format("priming failed: {}", primed.unwrap_err()));
    }

    auto operation_error = Option<String> {};
    auto calls           = rstd::uint64_t {};
    auto roundtrips      = rstd::uint64_t {};
    auto run_config      = loopback_run_config(1, payload_len);
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            if (! valid) {
                rstd::hint::black_box(valid);
                return;
            }
            auto result =
                runtime->block_on(loopback_ping_pong(*streams, payload, usize(LOOPBACK_BATCH)));
            if (result.is_err()) {
                operation_error = Some(rstd::format("operation failed: {}", result.unwrap_err()));
                valid           = false;
                return;
            }
            ++calls;
            roundtrips += LOOPBACK_BATCH;
            rstd::hint::black_box(roundtrips);
        },
        [&]() -> Result<empty, String> {
            if (operation_error.is_some()) return Err(rstd::move(*operation_error));
            if (! (valid && calls != 0 && roundtrips == calls * LOOPBACK_BATCH))
                return Err(String::make(rstd_bench::text("validation failed")));
            return Ok(empty {});
        });
}

auto io_loopback_ping_pong_4way(bench::BenchConfig config,
                                rstd::size_t       payload_len,
                                const char*        name,
                                IoBackend backend = IoBackend::Auto) -> rstd_bench::CaseRunResult {
    auto runtime = make_io_runtime(backend);
    if (runtime.is_err()) return runtime_failure(runtime.unwrap_err());
    auto listener = Option<net::TcpListener> {};
    auto streams  = Vec<LoopbackStreams>::with_capacity(usize(LOOPBACK_CONCURRENCY));
    auto payload  = loopback_payload(payload_len);
    bool valid    = true;

    if (valid) {
        auto bound = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
        if (bound.is_err()) {
            return rstd_bench::failed(rstd::format("bind failed: {}", bound.unwrap_err()));
        } else {
            listener = Some(rstd::move(bound).unwrap_unchecked());
        }
    }
    auto address = Option<net::SocketAddr> {};
    if (valid) {
        auto result = listener->local_addr();
        if (result.is_err()) {
            return rstd_bench::failed(rstd::format("address failed: {}", result.unwrap_err()));
        } else {
            address = Some(rstd::move(result).unwrap_unchecked());
        }
    }
    for (rstd::size_t index = 0; valid && index < LOOPBACK_CONCURRENCY; ++index) {
        auto opened = runtime->block_on(open_loopback_streams(*listener, *address, payload_len));
        if (opened.is_err()) {
            return rstd_bench::failed(rstd::format("connection failed: {}", opened.unwrap_err()));
        } else {
            streams.push(rstd::move(opened).unwrap_unchecked());
        }
    }
    if (valid) {
        auto primed = runtime->block_on(loopback_ping_pong_concurrent(streams, payload, usize(1)));
        if (primed.is_err())
            return rstd_bench::failed(rstd::format("priming failed: {}", primed.unwrap_err()));
    }

    auto operation_error = Option<String> {};
    auto calls           = rstd::uint64_t {};
    auto roundtrips      = rstd::uint64_t {};
    auto run_config      = loopback_run_config(LOOPBACK_CONCURRENCY, payload_len);
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            if (! valid) {
                rstd::hint::black_box(valid);
                return;
            }
            auto result = runtime->block_on(
                loopback_ping_pong_concurrent(streams, payload, usize(LOOPBACK_BATCH)));
            if (result.is_err()) {
                operation_error = Some(rstd::format("operation failed: {}", result.unwrap_err()));
                valid           = false;
                return;
            }
            ++calls;
            roundtrips += LOOPBACK_BATCH * LOOPBACK_CONCURRENCY;
            rstd::hint::black_box(roundtrips);
        },
        [&]() -> Result<empty, String> {
            if (operation_error.is_some()) return Err(rstd::move(*operation_error));
            if (! (valid && calls != 0 &&
                   roundtrips == calls * LOOPBACK_BATCH * LOOPBACK_CONCURRENCY))
                return Err(String::make(rstd_bench::text("validation failed")));
            return Ok(empty {});
        });
}

auto io_loopback_ping_pong_4worker(bench::BenchConfig config,
                                   rstd::size_t       payload_len,
                                   const char*        name,
                                   IoBackend          backend = IoBackend::Auto)
    -> rstd_bench::CaseRunResult {
    auto runtime = make_thread_pool_io_runtime(backend, usize(LOOPBACK_CONCURRENCY));
    if (runtime.is_err()) return runtime_failure(runtime.unwrap_err());
    auto listener = Option<net::TcpListener> {};
    auto streams  = Vec<LoopbackStreams>::with_capacity(usize(LOOPBACK_CONCURRENCY));
    auto payload  = loopback_payload(payload_len);
    bool valid    = true;

    if (valid) {
        auto bound = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
        if (bound.is_err()) {
            return rstd_bench::failed(rstd::format("bind failed: {}", bound.unwrap_err()));
        } else {
            listener = Some(rstd::move(bound).unwrap_unchecked());
        }
    }
    auto address = Option<net::SocketAddr> {};
    if (valid) {
        auto result = listener->local_addr();
        if (result.is_err()) {
            return rstd_bench::failed(rstd::format("address failed: {}", result.unwrap_err()));
        } else {
            address = Some(rstd::move(result).unwrap_unchecked());
        }
    }
    for (rstd::size_t index = 0; valid && index < LOOPBACK_CONCURRENCY; ++index) {
        auto opened = runtime->block_on(open_loopback_streams(*listener, *address, payload_len));
        if (opened.is_err()) {
            return rstd_bench::failed(rstd::format("connection failed: {}", opened.unwrap_err()));
        } else {
            streams.push(rstd::move(opened).unwrap_unchecked());
        }
    }
    if (valid) {
        auto primed = run_loopback_ping_pong_thread_pool(*runtime, streams, payload, usize(1));
        if (primed.is_err())
            return rstd_bench::failed(rstd::format("priming failed: {}", primed.unwrap_err()));
    }

    auto operation_error = Option<String> {};
    auto calls           = rstd::uint64_t {};
    auto roundtrips      = rstd::uint64_t {};
    auto run_config      = loopback_run_config(LOOPBACK_CONCURRENCY, payload_len);
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            if (! valid) {
                rstd::hint::black_box(valid);
                return;
            }
            auto result = run_loopback_ping_pong_thread_pool(
                *runtime, streams, payload, usize(LOOPBACK_BATCH));
            if (result.is_err()) {
                operation_error = Some(rstd::format("operation failed: {}", result.unwrap_err()));
                valid           = false;
                return;
            }
            ++calls;
            roundtrips += LOOPBACK_BATCH * LOOPBACK_CONCURRENCY;
            rstd::hint::black_box(roundtrips);
        },
        [&]() -> Result<empty, String> {
            if (operation_error.is_some()) return Err(rstd::move(*operation_error));
            if (! (valid && calls != 0 &&
                   roundtrips == calls * LOOPBACK_BATCH * LOOPBACK_CONCURRENCY))
                return Err(String::make(rstd_bench::text("validation failed")));
            return Ok(empty {});
        });
}

template<rstd::size_t PayloadLen, IoBackend Backend = IoBackend::Auto>
auto io_loopback_ping_pong_case(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    return io_loopback_ping_pong(rstd::move(config), PayloadLen, name, Backend);
}

template<rstd::size_t PayloadLen, IoBackend Backend = IoBackend::Auto>
auto io_loopback_ping_pong_4way_case(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    return io_loopback_ping_pong_4way(rstd::move(config), PayloadLen, name, Backend);
}

template<rstd::size_t PayloadLen>
auto io_loopback_ping_pong_4worker_case(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    return io_loopback_ping_pong_4worker(rstd::move(config), PayloadLen, name);
}

const rstd_bench::BenchCase CASES[] = {
#if RSTD_OS_LINUX
    { "async", "io_loopback_ping_pong_sync_1b", 5, &io_loopback_ping_pong_sync_case<1> },
    { "async", "io_loopback_ping_pong_sync_4way_1b", 5, &io_loopback_ping_pong_sync_4way_case<1> },
    { "async", "io_loopback_ping_pong_sync_1kib", 5, &io_loopback_ping_pong_sync_case<KIB> },
    { "async",
      "io_loopback_ping_pong_sync_4way_1kib",
      5,
      &io_loopback_ping_pong_sync_4way_case<KIB> },
    { "async", "io_loopback_ping_pong_sync_16kib", 5, &io_loopback_ping_pong_sync_case<KIB * 16> },
    { "async",
      "io_loopback_ping_pong_sync_4way_16kib",
      5,
      &io_loopback_ping_pong_sync_4way_case<KIB * 16> },
#endif
    { "async", "io_loopback_ping_pong_1b", 5, &io_loopback_ping_pong_case<1> },
    { "async", "io_loopback_ping_pong_4way_1b", 5, &io_loopback_ping_pong_4way_case<1> },
    { "async",
      "io_loopback_ping_pong_async_4worker_1b",
      5,
      &io_loopback_ping_pong_4worker_case<1> },
    { "async", "io_loopback_ping_pong_1kib", 5, &io_loopback_ping_pong_case<KIB> },
    { "async", "io_loopback_ping_pong_4way_1kib", 5, &io_loopback_ping_pong_4way_case<KIB> },
    { "async",
      "io_loopback_ping_pong_async_4worker_1kib",
      5,
      &io_loopback_ping_pong_4worker_case<KIB> },
    { "async", "io_loopback_ping_pong_16kib", 5, &io_loopback_ping_pong_case<KIB * 16> },
    { "async", "io_loopback_ping_pong_4way_16kib", 5, &io_loopback_ping_pong_4way_case<KIB * 16> },
    { "async",
      "io_loopback_ping_pong_async_4worker_16kib",
      5,
      &io_loopback_ping_pong_4worker_case<KIB * 16> },
#if RSTD_OS_LINUX
    { "async",
      "io_loopback_ping_pong_native_1b",
      5,
      &io_loopback_ping_pong_case<1, IoBackend::NativeCompletion> },
    { "async",
      "io_loopback_ping_pong_native_4way_1b",
      5,
      &io_loopback_ping_pong_4way_case<1, IoBackend::NativeCompletion> },
    { "async",
      "io_loopback_ping_pong_native_16kib",
      5,
      &io_loopback_ping_pong_case<KIB * 16, IoBackend::NativeCompletion> },
    { "async",
      "io_loopback_ping_pong_native_4way_16kib",
      5,
      &io_loopback_ping_pong_4way_case<KIB * 16, IoBackend::NativeCompletion> },
    { "async",
      "io_loopback_ping_pong_epoll_1b",
      5,
      &io_loopback_ping_pong_case<1, IoBackend::ReadinessEmulation> },
    { "async",
      "io_loopback_ping_pong_epoll_4way_1b",
      5,
      &io_loopback_ping_pong_4way_case<1, IoBackend::ReadinessEmulation> },
    { "async",
      "io_loopback_ping_pong_epoll_16kib",
      5,
      &io_loopback_ping_pong_case<KIB * 16, IoBackend::ReadinessEmulation> },
    { "async",
      "io_loopback_ping_pong_epoll_4way_16kib",
      5,
      &io_loopback_ping_pong_4way_case<KIB * 16, IoBackend::ReadinessEmulation> },
#endif
};

auto rstd_bench::async_loopback_benchmarks() -> BenchList {
    return { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}
