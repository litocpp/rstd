#include "benchmark.hpp"

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

    const u8 payload[] = { 'p', 'i', 'n', 'g' };
    auto     bytes     = bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(payload, 4));
    while (! bytes.is_empty()) {
        auto written = co_await write_some(client_stream, bytes);
        if (written.is_err()) {
            co_return Err(rstd::move(written).unwrap_err_unchecked());
        }
        bytes.advance(rstd::move(written).unwrap_unchecked());
    }

    auto received = bytes::BytesMut::with_capacity(4);
    while (received.len() < 4) {
        auto read = co_await read_some(server_stream, received);
        if (read.is_err()) {
            co_return Err(rstd::move(read).unwrap_err_unchecked());
        }
        if (rstd::move(read).unwrap_unchecked() == 0) {
            co_return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::UnexpectedEof }));
        }
    }

    co_return Ok(rstd::move(received));
}

auto loopback_roundtrip_4b(rstd_bench::BenchContext& context) -> bool {
    auto runtime = async::Runtime {};

    for (std::uint64_t i = 0; i < context.iterations(); ++i) {
        auto listener_result = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(0));
        if (listener_result.is_err()) {
            return false;
        }

        auto listener = rstd::move(listener_result).unwrap_unchecked();
        auto addr     = listener.local_addr();
        if (addr.is_err()) {
            return false;
        }

        auto result =
            runtime.block_on(tcp_roundtrip(listener, rstd::move(addr).unwrap_unchecked()));
        if (result.is_err()) {
            return false;
        }

        auto received = rstd::move(result).unwrap_unchecked();
        if (received.len() != 4 || received[0] != u8('p') || received[3] != u8('g')) {
            return false;
        }
        rstd::hint::black_box(received.len());
    }

    context.set_items_processed(context.iterations());
    context.set_bytes_processed(context.iterations() * 4);
    return true;
}

const rstd_bench::BenchCase CASES[] = {
    { "net", "loopback_roundtrip_4b", 500, 5, &loopback_roundtrip_4b },
};

} // namespace

namespace rstd_bench
{

auto net_benchmarks() -> BenchList {
    return BenchList { CASES, sizeof(CASES) / sizeof(CASES[0]) };
}

} // namespace rstd_bench
