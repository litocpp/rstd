export module rstd:async.stream;
export import :async.coro_future;
export import :bytes;
export import :forward;
export import :sync;
export import rstd.io;

using namespace rstd::prelude;

export namespace rstd::async::io
{
struct StreamReceive {
    ::rstd::io::Result<usize> result;
    bytes::BytesMut           buffer;
};

struct CompletionStream {
    template<class Self, class = void>
    struct Api {
        using Trait = CompletionStream;
        // Overwrite the buffer and return its storage; successful count equals buffer length.
        auto recv_into(bytes::BytesMut buffer) const -> coro<StreamReceive> {
            return trait_call<0>(this, rstd::move(buffer));
        }
        auto send(bytes::Bytes data) const -> coro<::rstd::io::Result<usize>> {
            return trait_call<1>(this, rstd::move(data));
        }
        auto flush() const -> coro<::rstd::io::Result<empty>> { return trait_call<2>(this); }
        auto shutdown_write() const -> coro<::rstd::io::Result<empty>> {
            return trait_call<3>(this);
        }
        void request_stop() const { trait_call<4>(this); }
        // Wait errors describe the barrier itself, not the stored release result.
        auto wait_closed() const -> coro<::rstd::io::Result<empty>> { return trait_call<5>(this); }
        auto take_close_result() const -> Option<::rstd::io::Result<empty>> {
            return trait_call<6>(this);
        }
    };
    template<class T>
    using Funcs = TraitFuncs<&T::recv_into,
                             &T::send,
                             &T::flush,
                             &T::shutdown_write,
                             &T::request_stop,
                             &T::wait_closed,
                             &T::take_close_result>;
};

// Operations retain their backend and buffers. One read and one write may run concurrently.
// Dropping an operation requests cancellation; close waits for the backend's real release.
class Stream {
    sync::Arc<dyn<CompletionStream>> backend_;

public:
    explicit Stream(sync::Arc<dyn<CompletionStream>> backend);
    Stream(const Stream&)                    = delete;
    auto operator=(const Stream&) -> Stream& = delete;
    Stream(Stream&&) noexcept;
    auto operator=(Stream&&) noexcept -> Stream&;
    ~Stream();
    auto recv_into(bytes::BytesMut buffer) const -> coro<StreamReceive>;
    auto recv(usize maximum_size = usize(65536), alloc::SharedAllocator allocator = {}) const
        -> coro<::rstd::io::Result<bytes::Bytes>>;
    auto send(bytes::Bytes data) const -> coro<::rstd::io::Result<usize>>;
    auto send_all(bytes::Bytes data) const -> coro<::rstd::io::Result<usize>>;
    auto flush() const -> coro<::rstd::io::Result<empty>>;
    auto shutdown_write() const -> coro<::rstd::io::Result<empty>>;
    void request_stop() const;
    auto wait_closed() const -> coro<::rstd::io::Result<empty>>;
    auto take_close_result() const -> Option<::rstd::io::Result<empty>>;
    // Waits for release and takes its result once. Other observers use wait_closed().
    auto close() const -> coro<::rstd::io::Result<empty>>;
};
} // namespace rstd::async::io
