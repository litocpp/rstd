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
using ::alloc::vec::Vec;

#if RSTD_OS_LINUX
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
            if (joined[index].is_err())
                co_return Err(io::error::Error::from_kind(
                    io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
            auto result = rstd::move(joined[index]).unwrap_unchecked();
            if (result.is_err()) co_return Err(rstd::move(result).unwrap_err_unchecked());
            auto completion = rstd::move(result).unwrap_unchecked();
            if (completion.transferred() != usize(1) || completion.data().len() != usize(1) ||
                completion.data()[usize()] != u8('q')) {
                co_return Err(io::error::Error::from_kind(
                    io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
            }
        }
    }
    co_return Ok(empty {});
}

template<rstd::size_t QueueDepth, bool Pending, IoOperationConsumer Consumer>
auto io_operation_read(bench::BenchConfig config, const char* name) -> rstd_bench::CaseRunResult {
    auto runtime = make_io_runtime(IoBackend::NativeCompletion);
    if (runtime.is_err()) return runtime_failure(runtime.unwrap_err());
    auto pairs = make_operation_read_pairs(QueueDepth);
    if (pairs.is_err())
        return rstd_bench::failed(rstd::format("socket setup failed: {}", pairs.unwrap_err()));
    bool valid = true;
    if (valid) {
        auto primed = runtime->block_on(run_operation_reads(*pairs, usize(1), Pending, Consumer));
        if (primed.is_err())
            return rstd_bench::failed(rstd::format("priming failed: {}", primed.unwrap_err()));
    }

    auto       operation_error = Option<String> {};
    auto       calls           = rstd::uint64_t {};
    auto       operations      = rstd::uint64_t {};
    auto const batch           = LOOPBACK_BATCH * QueueDepth;
    auto       run_config      = bench::RunConfig {
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
                operation_error = Some(rstd::format("operation failed: {}", result.unwrap_err()));
                valid           = false;
                return;
            }
            ++calls;
            operations += batch;
            rstd::hint::black_box(operations);
        },
        [&]() -> Result<empty, String> {
            if (operation_error.is_some()) return Err(rstd::move(*operation_error));
            if (! (valid && calls != 0 && operations == calls * batch))
                return Err(String::make(rstd_bench::text("validation failed")));
            return Ok(empty {});
        });
}
#endif

#if RSTD_OS_LINUX
const rstd_bench::BenchCase CASES[] = {
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
};

#endif

auto rstd_bench::async_io_benchmarks() -> BenchList {
#if RSTD_OS_LINUX
    return { CASES, sizeof(CASES) / sizeof(CASES[0]) };
#else
    return { nullptr, 0 };
#endif
}
