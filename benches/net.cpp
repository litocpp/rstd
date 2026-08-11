module rstd.benchmark;
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

auto would_block(io::error::Error const& error) -> bool {
    return error.kind() == io::error::ErrorKind { io::error::ErrorKind::WouldBlock };
}

async::coro<io::Result<usize>> write_some(net::TcpStream& stream, bytes::Bytes const& bytes) {
    while (true) {
        auto written = stream.try_write(bytes);
        if (written.is_ok()) {
            co_return written;
        }

        auto error = rstd::move(written).unwrap_err_unchecked();
        if (! would_block(error)) {
            co_return Err(rstd::move(error));
        }

        auto ready = co_await stream.writable();
        if (ready.is_err()) {
            co_return Err(rstd::move(ready).unwrap_err_unchecked());
        }
    }
}

async::coro<io::Result<usize>> read_some(net::TcpStream& stream, bytes::BytesMut& buf) {
    while (true) {
        auto read = stream.try_read(buf);
        if (read.is_ok()) {
            co_return read;
        }

        auto error = rstd::move(read).unwrap_err_unchecked();
        if (! would_block(error)) {
            co_return Err(rstd::move(error));
        }

        auto ready = co_await stream.readable();
        if (ready.is_err()) {
            co_return Err(rstd::move(ready).unwrap_err_unchecked());
        }
    }
}

async::coro<io::Result<bytes::BytesMut>> tcp_roundtrip(net::TcpListener& listener,
                                                       net::SocketAddr   addr) {
    auto client = co_await net::TcpStream::connect(addr);
    if (client.is_err()) {
        co_return Err(rstd::move(client).unwrap_err_unchecked());
    }

    auto accepted = co_await listener.accept();
    if (accepted.is_err()) {
        co_return Err(rstd::move(accepted).unwrap_err_unchecked());
    }

    auto client_stream = rstd::move(client).unwrap_unchecked();
    auto accepted_pair = rstd::move(accepted).unwrap_unchecked();
    auto server_stream = rstd::move(accepted_pair.template get<0>());

    const byte payload[] = { byte { 'p' }, byte { 'i' }, byte { 'n' }, byte { 'g' } };
    auto       bytes = bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(payload, usize(4)));
    while (! bytes.is_empty()) {
        auto written = co_await write_some(client_stream, bytes);
        if (written.is_err()) {
            co_return Err(rstd::move(written).unwrap_err_unchecked());
        }
        bytes.advance(rstd::move(written).unwrap_unchecked());
    }

    auto received = bytes::BytesMut::with_capacity(usize(4));
    while (received.len() < usize(4)) {
        auto read = co_await read_some(server_stream, received);
        if (read.is_err()) {
            co_return Err(rstd::move(read).unwrap_err_unchecked());
        }
        if (rstd::move(read).unwrap_unchecked() == usize()) {
            co_return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::UnexpectedEof }));
        }
    }

    co_return Ok(rstd::move(received));
}

auto tcp_connect_accept_readiness_roundtrip_4b(bench::BenchConfig config, const char* name)
    -> rstd_bench::CaseRunResult {
    auto runtime    = async::Runtime {};
    bool valid      = true;
    auto run_config = bench::RunConfig {
        .items_per_iteration = u64(1),
        .bytes_per_iteration = u64(4),
    };
    return rstd_bench::measure_case(
        name,
        rstd::move(config),
        rstd::move(run_config),
        [&] {
            auto listener_result = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
            if (listener_result.is_err()) {
                valid = false;
                return;
            }

            auto listener = rstd::move(listener_result).unwrap_unchecked();
            auto addr     = listener.local_addr();
            if (addr.is_err()) {
                valid = false;
                return;
            }

            auto result =
                runtime.block_on(tcp_roundtrip(listener, rstd::move(addr).unwrap_unchecked()));
            if (result.is_err()) {
                valid = false;
                return;
            }

            auto received = rstd::move(result).unwrap_unchecked();
            if (received.len() != usize(4) || received[usize()] != u8('p') ||
                received[usize(3)] != u8('g')) {
                valid = false;
                return;
            }
            rstd::hint::black_box(received.len());
        },
        [&] {
            return valid;
        });
}

const rstd_bench::BenchCase CASES[] = {
    { "net",
      "tcp_connect_accept_readiness_roundtrip_4b",
      5,
      &tcp_connect_accept_readiness_roundtrip_4b },
};

} // namespace

namespace rstd_bench
{

auto net_benchmarks() -> BenchList {
    return BenchList { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}

} // namespace rstd_bench
