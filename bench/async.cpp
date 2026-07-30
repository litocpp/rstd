#include <rstd/macro.hpp>

#if RSTD_OS_LINUX
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include "benchmark.hpp"

import rstd;

using namespace rstd;
using namespace rstd::prelude;
using ::alloc::vec::Vec;

namespace
{

extern "C" void rstd_async_bench_set_io_backend(async::RuntimeBuilder& builder, int backend);

enum class IoBackend
{
    Auto,
    NativeCompletion,
    ReadinessEmulation,
};

auto make_io_runtime(IoBackend backend) -> io::Result<async::Runtime> {
    auto builder = async::RuntimeBuilder::current_thread();
    builder.enable_io();
    rstd_async_bench_set_io_backend(builder, static_cast<int>(backend));
    return builder.build();
}

auto make_thread_pool_io_runtime(IoBackend backend, usize worker_threads)
    -> io::Result<async::Runtime> {
    auto builder = async::RuntimeBuilder::multi_thread();
    builder.worker_threads(worker_threads).enable_io();
    rstd_async_bench_set_io_backend(builder, static_cast<int>(backend));
    return builder.build();
}

inline constexpr rstd::size_t LOOPBACK_BATCH       = 64;
inline constexpr rstd::size_t LOOPBACK_CONCURRENCY = 4;
inline constexpr rstd::size_t KIB                  = 1024;

struct ReadyInt {
    using Output = int;

    auto poll(mut_ref<ReadyInt>, task::Context&) -> task::Poll<int> {
        return task::Poll<int>::Ready(1);
    }
};

async::coro<int> child_value() {
    co_await async::yield_now();
    co_return 1;
}

async::coro<int> indexed_child_value(int value) {
    co_await async::yield_now();
    co_return value;
}

async::coro<int> join_local_child() {
    auto handle = async::spawn_local(child_value());
    auto result = co_await rstd::move(handle);
    co_return result.unwrap_unchecked();
}

async::coro<int> join_spawned_child() {
    auto handle = async::spawn(child_value());
    auto result = co_await rstd::move(handle);
    co_return result.unwrap_unchecked();
}

async::coro<int> join_many_spawned_children() {
    auto handles = Vec<async::JoinHandle<int>>::make();
    for (int i = 0; i < 32; ++i) {
        handles.push(async::spawn(indexed_child_value(i)));
    }

    auto results = co_await async::join_all(rstd::move(handles));
    int  sum     = 0;
    for (usize i; i < results.len(); ++i) {
        sum += results[i].unwrap_unchecked();
    }
    co_return sum;
}

async::coro<int> sleep_zero() {
    co_await async::sleep(time::Duration::from_millis(u64()));
    co_return 1;
}

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
    std::uint64_t generation {};
    usize         count {};
    usize         completed {};
    bool          stop {};
    bool          valid { true };
};

struct SyncConcurrentState {
    sync::Mutex<SyncConcurrentFields> fields;
    sync::Condvar                     changed;

    SyncConcurrentState(): fields(SyncConcurrentFields {}), changed() {}
};

auto sync_loopback_worker(sync::Arc<SyncConcurrentState> state,
                          SyncLoopbackStreams            streams,
                          rstd::size_t                   payload_len) -> bool {
    auto          payload    = loopback_payload(payload_len);
    std::uint64_t generation = 0;
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

enum class IoOperationConsumer
{
    Direct,
    Future,
};

struct FutureIoOperation {
    using Output = async::IoOperation::Output;

    async::IoOperation operation;

    auto poll(mut_ref<FutureIoOperation> self, task::Context& cx) -> task::Poll<Output> {
        return future::poll(self->operation, cx);
    }
};

struct OperationReadPair {
    os::socket::OwnedSocket reader;
    os::socket::OwnedSocket writer;
    async::CompletionSource source;

    OperationReadPair(os::socket::OwnedSocket reader, os::socket::OwnedSocket writer)
        : reader(rstd::move(reader)),
          writer(rstd::move(writer)),
          source(async::CompletionSource::socket(this->reader.as_socket())) {}
};

auto make_operation_read_pairs(rstd::size_t queue_depth) -> io::Result<Vec<OperationReadPair>> {
    auto pairs = Vec<OperationReadPair>::with_capacity(usize(queue_depth));
    for (rstd::size_t index = 0; index < queue_depth; ++index) {
        int sockets[2] = { -1, -1 };
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) != 0) {
            return Err(io::error::Error::last_os_error());
        }
        pairs.push(OperationReadPair {
            os::socket::OwnedSocket::from_raw_socket(sockets[0]),
            os::socket::OwnedSocket::from_raw_socket(sockets[1]),
        });
    }
    return Ok(rstd::move(pairs));
}

auto send_operation_read_bytes(Vec<OperationReadPair>& pairs) -> io::Result<empty> {
    constexpr char VALUE = 'q';
    for (usize index {}; index < pairs.len(); ++index) {
        ::ssize_t sent;
        do {
            sent = ::send(pairs[index].writer.as_raw_socket(), &VALUE, 1, MSG_NOSIGNAL);
        } while (sent < 0 && errno == EINTR);
        if (sent != 1) return Err(io::error::Error::last_os_error());
    }
    return Ok(empty {});
}

auto run_operation_reads(Vec<OperationReadPair>& pairs,
                         usize                   rounds,
                         bool                    pending,
                         IoOperationConsumer     consumer) -> async::coro<io::Result<empty>> {
    for (usize round {}; round < rounds; ++round) {
        if (! pending) {
            auto sent = send_operation_read_bytes(pairs);
            if (sent.is_err()) co_return Err(rstd::move(sent).unwrap_err_unchecked());
        }

        auto handles =
            Vec<async::JoinHandle<async::IoOperation::Output>>::with_capacity(pairs.len());
        for (usize index {}; index < pairs.len(); ++index) {
            auto operation = async::IoOperation::read(pairs[index].source, usize(1));
            if (consumer == IoOperationConsumer::Direct) {
                handles.push(async::spawn_local(rstd::move(operation)));
            } else {
                handles.push(async::spawn_local(FutureIoOperation { rstd::move(operation) }));
            }
        }

        if (pending) {
            co_await async::yield_now();
            auto sent = send_operation_read_bytes(pairs);
            if (sent.is_err()) co_return Err(rstd::move(sent).unwrap_err_unchecked());
        }

        auto joined = co_await async::join_all(rstd::move(handles));
        for (usize index {}; index < joined.len(); ++index) {
            if (joined[index].is_err()) co_return Err(invalid_loopback_data());
            auto result = rstd::move(joined[index]).unwrap_unchecked();
            if (result.is_err()) co_return Err(rstd::move(result).unwrap_err_unchecked());
            auto completion = rstd::move(result).unwrap_unchecked();
            if (completion.transferred() != usize(1) || completion.data().len() != usize(1) ||
                completion.data()[usize()] != u8('q')) {
                co_return Err(invalid_loopback_data());
            }
        }
    }
    co_return Ok(empty {});
}

template<rstd::size_t QueueDepth, bool Pending, IoOperationConsumer Consumer>
auto io_operation_read(bench::BenchConfig config, const char* name) -> rstd_bench::CaseRunResult {
    auto runtime = make_io_runtime(IoBackend::NativeCompletion).ok();
    auto pairs   = make_operation_read_pairs(QueueDepth).ok();
    bool valid   = runtime.is_some() && pairs.is_some();
    if (valid) {
        auto primed = runtime->block_on(run_operation_reads(*pairs, usize(1), Pending, Consumer));
        valid       = primed.is_ok();
    }

    auto       calls      = std::uint64_t {};
    auto       operations = std::uint64_t {};
    auto const batch      = LOOPBACK_BATCH * QueueDepth;
    auto       run_config = bench::RunConfig {
        .batch               = f64(static_cast<double>(batch)),
        .items_per_iteration = u64(batch),
        .bytes_per_iteration = u64(batch),
    };
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
                run_operation_reads(*pairs, usize(LOOPBACK_BATCH), Pending, Consumer));
            if (result.is_err()) {
                valid = false;
                return;
            }
            ++calls;
            operations += batch;
            rstd::hint::black_box(operations);
        },
        [&] {
            return valid && calls != 0 && operations == calls * batch;
        });
}
#endif

auto current_thread_ready(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    auto runtime    = async::Runtime {};
    auto sum        = std::uint64_t {};
    auto calls      = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            sum += runtime.block_on(ReadyInt {});
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&] {
            return sum == calls;
        });
}

auto current_thread_spawn_local_join(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    auto runtime    = async::Runtime {};
    auto sum        = std::uint64_t {};
    auto calls      = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            sum += runtime.block_on(join_local_child());
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&] {
            return sum == calls;
        });
}

auto thread_pool_spawn_join(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    auto runtime    = async::RuntimeBuilder::multi_thread().worker_threads(usize(2)).build().ok();
    auto sum        = std::uint64_t {};
    auto calls      = std::uint64_t {};
    bool valid      = runtime.is_some();
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            if (runtime.is_none()) return;
            sum += runtime->block_on(join_spawned_child());
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&] {
            return valid && sum == calls;
        });
}

auto thread_pool_join_many(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    auto runtime    = async::RuntimeBuilder::multi_thread().worker_threads(usize(4)).build().ok();
    auto sum        = std::uint64_t {};
    auto calls      = std::uint64_t {};
    bool valid      = runtime.is_some();
    auto run_config = bench::RunConfig { .items_per_iteration = u64(32) };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            if (runtime.is_none()) return;
            sum += runtime->block_on(join_many_spawned_children());
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&] {
            return valid && sum == calls * 496;
        });
}

auto timer_sleep_zero(bench::BenchConfig config, const char* name) -> rstd_bench::CaseRunResult {
    auto runtime    = async::Runtime {};
    auto sum        = std::uint64_t {};
    auto calls      = std::uint64_t {};
    auto run_config = bench::RunConfig { .items_per_iteration = u64(1) };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            sum += runtime.block_on(sleep_zero());
            ++calls;
            rstd::hint::black_box(sum);
        },
        [&] {
            return sum == calls;
        });
}

#if RSTD_OS_LINUX
auto io_loopback_ping_pong_sync(bench::BenchConfig config,
                                rstd::size_t       payload_len,
                                const char*        name) -> rstd_bench::CaseRunResult {
    auto opened  = open_sync_loopback_streams(payload_len);
    auto streams = rstd::move(opened).ok();
    auto payload = loopback_payload(payload_len);
    bool valid   = streams.is_some() && sync_loopback_ping_pong(*streams, payload, usize(1));

    auto calls      = std::uint64_t {};
    auto roundtrips = std::uint64_t {};
    auto run_config = loopback_run_config(1, payload_len);
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
        [&] {
            return valid && calls != 0 && roundtrips == calls * LOOPBACK_BATCH;
        });
}

auto io_loopback_ping_pong_sync_4way(bench::BenchConfig config,
                                     rstd::size_t       payload_len,
                                     const char*        name) -> rstd_bench::CaseRunResult {
    auto state   = sync::Arc<SyncConcurrentState>::make();
    auto handles = Vec<thread::JoinHandle<bool>>::with_capacity(usize(LOOPBACK_CONCURRENCY));
    bool valid   = true;

    for (rstd::size_t index = 0; index < LOOPBACK_CONCURRENCY; ++index) {
        auto opened = open_sync_loopback_streams(payload_len);
        if (opened.is_err()) {
            valid = false;
            break;
        }
        auto worker_state = state.clone();
        auto spawned      = thread::spawn([state   = rstd::move(worker_state),
                                           streams = rstd::move(opened).unwrap_unchecked(),
                                           payload_len]() mutable {
            return sync_loopback_worker(rstd::move(state), rstd::move(streams), payload_len);
        });
        if (spawned.is_err()) {
            valid = false;
            break;
        }
        handles.push(rstd::move(spawned).unwrap_unchecked());
    }
    if (valid) valid = run_sync_loopback_concurrent(state, usize(1));

    auto calls      = std::uint64_t {};
    auto roundtrips = std::uint64_t {};
    auto run_config = loopback_run_config(LOOPBACK_CONCURRENCY, payload_len);
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
        [&] {
            {
                auto guard  = state->fields.lock().unwrap_unchecked();
                guard->stop = true;
                state->changed.notify_all();
            }

            bool joined = handles.len() == usize(LOOPBACK_CONCURRENCY);
            for (usize index {}; index < handles.len(); ++index) {
                auto result = rstd::move(handles[index]).join();
                if (result.is_err() || ! rstd::move(result).unwrap_unchecked()) joined = false;
            }
            return valid && joined && calls != 0 &&
                   roundtrips == calls * LOOPBACK_BATCH * LOOPBACK_CONCURRENCY;
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
    auto runtime  = make_io_runtime(backend).ok();
    auto listener = Option<net::TcpListener> {};
    auto streams  = Option<LoopbackStreams> {};
    auto payload  = loopback_payload(payload_len);
    bool valid    = runtime.is_some();

    if (valid) {
        auto bound = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
        if (bound.is_err()) {
            valid = false;
        } else {
            listener = Some(rstd::move(bound).unwrap_unchecked());
        }
    }
    if (valid) {
        auto address = listener->local_addr();
        if (address.is_err()) {
            valid = false;
        } else {
            auto opened = runtime->block_on(open_loopback_streams(
                *listener, rstd::move(address).unwrap_unchecked(), payload_len));
            if (opened.is_err()) {
                valid = false;
            } else {
                streams = Some(rstd::move(opened).unwrap_unchecked());
            }
        }
    }
    if (valid) {
        auto primed = runtime->block_on(loopback_ping_pong(*streams, payload, usize(1)));
        valid       = primed.is_ok();
    }

    auto calls      = std::uint64_t {};
    auto roundtrips = std::uint64_t {};
    auto run_config = loopback_run_config(1, payload_len);
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
                valid = false;
                return;
            }
            ++calls;
            roundtrips += LOOPBACK_BATCH;
            rstd::hint::black_box(roundtrips);
        },
        [&] {
            return valid && calls != 0 && roundtrips == calls * LOOPBACK_BATCH;
        });
}

auto io_loopback_ping_pong_4way(bench::BenchConfig config,
                                rstd::size_t       payload_len,
                                const char*        name,
                                IoBackend backend = IoBackend::Auto) -> rstd_bench::CaseRunResult {
    auto runtime  = make_io_runtime(backend).ok();
    auto listener = Option<net::TcpListener> {};
    auto streams  = Vec<LoopbackStreams>::with_capacity(usize(LOOPBACK_CONCURRENCY));
    auto payload  = loopback_payload(payload_len);
    bool valid    = runtime.is_some();

    if (valid) {
        auto bound = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
        if (bound.is_err()) {
            valid = false;
        } else {
            listener = Some(rstd::move(bound).unwrap_unchecked());
        }
    }
    auto address = Option<net::SocketAddr> {};
    if (valid) {
        auto result = listener->local_addr();
        if (result.is_err()) {
            valid = false;
        } else {
            address = Some(rstd::move(result).unwrap_unchecked());
        }
    }
    for (rstd::size_t index = 0; valid && index < LOOPBACK_CONCURRENCY; ++index) {
        auto opened = runtime->block_on(open_loopback_streams(*listener, *address, payload_len));
        if (opened.is_err()) {
            valid = false;
        } else {
            streams.push(rstd::move(opened).unwrap_unchecked());
        }
    }
    if (valid) {
        auto primed = runtime->block_on(loopback_ping_pong_concurrent(streams, payload, usize(1)));
        valid       = primed.is_ok();
    }

    auto calls      = std::uint64_t {};
    auto roundtrips = std::uint64_t {};
    auto run_config = loopback_run_config(LOOPBACK_CONCURRENCY, payload_len);
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
                valid = false;
                return;
            }
            ++calls;
            roundtrips += LOOPBACK_BATCH * LOOPBACK_CONCURRENCY;
            rstd::hint::black_box(roundtrips);
        },
        [&] {
            return valid && calls != 0 &&
                   roundtrips == calls * LOOPBACK_BATCH * LOOPBACK_CONCURRENCY;
        });
}

auto io_loopback_ping_pong_4worker(bench::BenchConfig config,
                                   rstd::size_t       payload_len,
                                   const char*        name,
                                   IoBackend          backend = IoBackend::Auto)
    -> rstd_bench::CaseRunResult {
    auto runtime  = make_thread_pool_io_runtime(backend, usize(LOOPBACK_CONCURRENCY)).ok();
    auto listener = Option<net::TcpListener> {};
    auto streams  = Vec<LoopbackStreams>::with_capacity(usize(LOOPBACK_CONCURRENCY));
    auto payload  = loopback_payload(payload_len);
    bool valid    = runtime.is_some();

    if (valid) {
        auto bound = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
        if (bound.is_err()) {
            valid = false;
        } else {
            listener = Some(rstd::move(bound).unwrap_unchecked());
        }
    }
    auto address = Option<net::SocketAddr> {};
    if (valid) {
        auto result = listener->local_addr();
        if (result.is_err()) {
            valid = false;
        } else {
            address = Some(rstd::move(result).unwrap_unchecked());
        }
    }
    for (rstd::size_t index = 0; valid && index < LOOPBACK_CONCURRENCY; ++index) {
        auto opened = runtime->block_on(open_loopback_streams(*listener, *address, payload_len));
        if (opened.is_err()) {
            valid = false;
        } else {
            streams.push(rstd::move(opened).unwrap_unchecked());
        }
    }
    if (valid) {
        auto primed = run_loopback_ping_pong_thread_pool(*runtime, streams, payload, usize(1));
        valid       = primed.is_ok();
    }

    auto calls      = std::uint64_t {};
    auto roundtrips = std::uint64_t {};
    auto run_config = loopback_run_config(LOOPBACK_CONCURRENCY, payload_len);
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
                valid = false;
                return;
            }
            ++calls;
            roundtrips += LOOPBACK_BATCH * LOOPBACK_CONCURRENCY;
            rstd::hint::black_box(roundtrips);
        },
        [&] {
            return valid && calls != 0 &&
                   roundtrips == calls * LOOPBACK_BATCH * LOOPBACK_CONCURRENCY;
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
    { "async", "current_thread_ready", 1'000, &current_thread_ready },
    { "async", "current_thread_spawn_local_join", 500, &current_thread_spawn_local_join },
    { "async", "thread_pool_spawn_join_2", 200, &thread_pool_spawn_join },
    { "async", "thread_pool_join_many_4x32", 20, &thread_pool_join_many },
    { "async", "timer_sleep_zero", 500, &timer_sleep_zero },
#if RSTD_OS_LINUX
    { "async",
      "io_loopback_ping_pong_sync_1b",
      5,
      &io_loopback_ping_pong_sync_case<1> },
    { "async",
      "io_loopback_ping_pong_sync_4way_1b",
      5,
      &io_loopback_ping_pong_sync_4way_case<1> },
    { "async",
      "io_loopback_ping_pong_sync_1kib",
      5,
      &io_loopback_ping_pong_sync_case<KIB> },
    { "async",
      "io_loopback_ping_pong_sync_4way_1kib",
      5,
      &io_loopback_ping_pong_sync_4way_case<KIB> },
    { "async",
      "io_loopback_ping_pong_sync_16kib",
      5,
      &io_loopback_ping_pong_sync_case<KIB * 16> },
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
    { "async",
      "io_loopback_ping_pong_4way_16kib",
      5,
      &io_loopback_ping_pong_4way_case<KIB * 16> },
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
    { "async",
      "io_operation_direct_immediate_qd1",
      2,
      &io_operation_read<1, false, IoOperationConsumer::Direct> },
    { "async",
      "io_operation_direct_immediate_qd64",
      2,
      &io_operation_read<64, false, IoOperationConsumer::Direct> },
    { "async",
      "io_operation_direct_pending_qd1",
      2,
      &io_operation_read<1, true, IoOperationConsumer::Direct> },
    { "async",
      "io_operation_direct_pending_qd64",
      2,
      &io_operation_read<64, true, IoOperationConsumer::Direct> },
    { "async",
      "io_operation_future_immediate_qd1",
      2,
      &io_operation_read<1, false, IoOperationConsumer::Future> },
    { "async",
      "io_operation_future_immediate_qd64",
      2,
      &io_operation_read<64, false, IoOperationConsumer::Future> },
    { "async",
      "io_operation_future_pending_qd1",
      2,
      &io_operation_read<1, true, IoOperationConsumer::Future> },
    { "async",
      "io_operation_future_pending_qd64",
      2,
      &io_operation_read<64, true, IoOperationConsumer::Future> },
#endif
};

} // namespace

namespace rstd_bench
{

auto async_benchmarks() -> BenchList {
    return BenchList { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}

} // namespace rstd_bench
