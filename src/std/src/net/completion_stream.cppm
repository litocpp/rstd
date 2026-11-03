export module rstd:net.completion_stream;
export import :net.tcp;
export import :async.stream;

export namespace rstd::net
{
auto as_stream(TcpStream stream, alloc::SharedAllocator allocator = {})
    -> io::Result<async::io::Stream>;
}
