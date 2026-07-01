export module rstd:async.io;
export import :async.forward;
import :bytes;
import :io;

namespace rstd::async::io
{

export struct AsyncRead {
    template<class Self, class Delegate = void>
    struct Api {
        using Trait = AsyncRead;

        auto poll_read(pin::Pin<mut_ref<Self>> self,
                       task::Context&          cx,
                       bytes::BytesMut&        buf) -> task::Poll<::rstd::io::Result<usize>> {
            return trait_call<0>(this, self, cx, buf);
        }
    };

    template<typename F>
    using Funcs = TraitFuncs<&F::poll_read>;
};

export struct AsyncWrite {
    template<class Self, class Delegate = void>
    struct Api {
        using Trait = AsyncWrite;

        auto poll_write(pin::Pin<mut_ref<Self>> self,
                        task::Context&          cx,
                        const bytes::Bytes&     buf) -> task::Poll<::rstd::io::Result<usize>> {
            return trait_call<0>(this, self, cx, buf);
        }

        auto poll_flush(pin::Pin<mut_ref<Self>> self, task::Context& cx)
            -> task::Poll<::rstd::io::Result<empty>> {
            return trait_call<1>(this, self, cx);
        }

        auto poll_shutdown(pin::Pin<mut_ref<Self>> self, task::Context& cx)
            -> task::Poll<::rstd::io::Result<empty>> {
            return trait_call<2>(this, self, cx);
        }
    };

    template<typename F>
    using Funcs = TraitFuncs<&F::poll_write, &F::poll_flush, &F::poll_shutdown>;
};

export template<typename R>
concept AsyncReadInClass = requires(mtp::rm_cvf<R>& reader,
                                    task::Context&  cx,
                                    bytes::BytesMut& buf) {
    { reader.poll_read(future::pin_mut(reader), cx, buf) }
        -> mtp::same_as<task::Poll<::rstd::io::Result<usize>>>;
};

export template<typename R>
concept AsyncReadLike = AsyncReadInClass<R> || Impled<mtp::rm_cvf<R>, AsyncRead>;

export template<typename W>
concept AsyncWriteInClass = requires(mtp::rm_cvf<W>& writer,
                                     task::Context&  cx,
                                     const bytes::Bytes& buf) {
    { writer.poll_write(future::pin_mut(writer), cx, buf) }
        -> mtp::same_as<task::Poll<::rstd::io::Result<usize>>>;
    { writer.poll_flush(future::pin_mut(writer), cx) }
        -> mtp::same_as<task::Poll<::rstd::io::Result<empty>>>;
    { writer.poll_shutdown(future::pin_mut(writer), cx) }
        -> mtp::same_as<task::Poll<::rstd::io::Result<empty>>>;
};

export template<typename W>
concept AsyncWriteLike = AsyncWriteInClass<W> || Impled<mtp::rm_cvf<W>, AsyncWrite>;

export template<typename R>
auto poll_read(R& reader, task::Context& cx, bytes::BytesMut& buf)
    -> task::Poll<::rstd::io::Result<usize>>
    requires AsyncReadLike<R>
{
    if constexpr (AsyncReadInClass<R>) {
        return reader.poll_read(future::pin_mut(reader), cx, buf);
    } else {
        return as<AsyncRead>(reader).poll_read(future::pin_mut(reader), cx, buf);
    }
}

export template<typename W>
auto poll_write(W& writer, task::Context& cx, const bytes::Bytes& buf)
    -> task::Poll<::rstd::io::Result<usize>>
    requires AsyncWriteLike<W>
{
    if constexpr (AsyncWriteInClass<W>) {
        return writer.poll_write(future::pin_mut(writer), cx, buf);
    } else {
        return as<AsyncWrite>(writer).poll_write(future::pin_mut(writer), cx, buf);
    }
}

export template<typename W>
auto poll_flush(W& writer, task::Context& cx) -> task::Poll<::rstd::io::Result<empty>>
    requires AsyncWriteLike<W>
{
    if constexpr (AsyncWriteInClass<W>) {
        return writer.poll_flush(future::pin_mut(writer), cx);
    } else {
        return as<AsyncWrite>(writer).poll_flush(future::pin_mut(writer), cx);
    }
}

export template<typename W>
auto poll_shutdown(W& writer, task::Context& cx) -> task::Poll<::rstd::io::Result<empty>>
    requires AsyncWriteLike<W>
{
    if constexpr (AsyncWriteInClass<W>) {
        return writer.poll_shutdown(future::pin_mut(writer), cx);
    } else {
        return as<AsyncWrite>(writer).poll_shutdown(future::pin_mut(writer), cx);
    }
}

export template<AsyncReadLike R>
class ReadFuture {
    R*               reader;
    bytes::BytesMut* buf;
    bool             completed { false };

public:
    using Output = ::rstd::io::Result<usize>;

    ReadFuture(R& reader, bytes::BytesMut& buf)
        : reader(rstd::addressof(reader)), buf(rstd::addressof(buf)) {}

    ReadFuture(const ReadFuture&)            = delete;
    auto operator=(const ReadFuture&) -> ReadFuture& = delete;
    ReadFuture(ReadFuture&&) noexcept                    = default;
    auto operator=(ReadFuture&&) noexcept -> ReadFuture& = default;

    auto poll(pin::Pin<mut_ref<ReadFuture>> self, task::Context& cx) -> task::Poll<Output> {
        auto& future = *self.get_unchecked_mut();
        if (future.completed) {
            rstd::panic { "async::io::ReadFuture polled after completion" };
        }

        auto out = poll_read(*future.reader, cx, *future.buf);
        if (out.is_ready()) {
            future.completed = true;
        }
        return out;
    }
};

export template<AsyncWriteLike W>
class WriteFuture {
    W*                  writer;
    const bytes::Bytes* buf;
    bool                completed { false };

public:
    using Output = ::rstd::io::Result<usize>;

    WriteFuture(W& writer, const bytes::Bytes& buf)
        : writer(rstd::addressof(writer)), buf(rstd::addressof(buf)) {}

    WriteFuture(const WriteFuture&)            = delete;
    auto operator=(const WriteFuture&) -> WriteFuture& = delete;
    WriteFuture(WriteFuture&&) noexcept                    = default;
    auto operator=(WriteFuture&&) noexcept -> WriteFuture& = default;

    auto poll(pin::Pin<mut_ref<WriteFuture>> self, task::Context& cx) -> task::Poll<Output> {
        auto& future = *self.get_unchecked_mut();
        if (future.completed) {
            rstd::panic { "async::io::WriteFuture polled after completion" };
        }

        auto out = poll_write(*future.writer, cx, *future.buf);
        if (out.is_ready()) {
            future.completed = true;
        }
        return out;
    }
};

export template<AsyncReadLike R>
class ReadExactFuture {
    R*               reader;
    bytes::BytesMut* buf;
    usize           target_len {};
    usize           start_len {};
    bool            started { false };
    bool            completed { false };

public:
    using Output = ::rstd::io::Result<empty>;

    ReadExactFuture(R& reader, bytes::BytesMut& buf, usize len)
        : reader(rstd::addressof(reader)), buf(rstd::addressof(buf)), target_len(len) {}

    ReadExactFuture(const ReadExactFuture&)            = delete;
    auto operator=(const ReadExactFuture&) -> ReadExactFuture& = delete;
    ReadExactFuture(ReadExactFuture&&) noexcept                    = default;
    auto operator=(ReadExactFuture&&) noexcept -> ReadExactFuture& = default;

    auto poll(pin::Pin<mut_ref<ReadExactFuture>> self, task::Context& cx)
        -> task::Poll<Output> {
        auto& future = *self.get_unchecked_mut();
        if (future.completed) {
            rstd::panic { "async::io::ReadExactFuture polled after completion" };
        }
        if (! future.started) {
            future.started   = true;
            future.start_len = future.buf->len();
        }

        while (future.buf->len() - future.start_len < future.target_len) {
            auto out = poll_read(*future.reader, cx, *future.buf);
            if (out.is_pending()) {
                return task::Poll<Output>::Pending();
            }

            auto result = rstd::move(out).take();
            if (result.is_err()) {
                future.completed = true;
                return task::Poll<Output>::Ready(Err(rstd::move(result).unwrap_err_unchecked()));
            }
            if (rstd::move(result).unwrap_unchecked() == 0) {
                future.completed = true;
                return task::Poll<Output>::Ready(Err(::rstd::io::error::Error_READ_EXACT_EOF));
            }
        }

        future.completed = true;
        return task::Poll<Output>::Ready(Ok(empty {}));
    }
};

export template<AsyncWriteLike W>
class WriteAllFuture {
    W*                  writer;
    const bytes::Bytes* buf;
    usize              written {};
    bool               completed { false };

public:
    using Output = ::rstd::io::Result<empty>;

    WriteAllFuture(W& writer, const bytes::Bytes& buf)
        : writer(rstd::addressof(writer)), buf(rstd::addressof(buf)) {}

    WriteAllFuture(const WriteAllFuture&)            = delete;
    auto operator=(const WriteAllFuture&) -> WriteAllFuture& = delete;
    WriteAllFuture(WriteAllFuture&&) noexcept                    = default;
    auto operator=(WriteAllFuture&&) noexcept -> WriteAllFuture& = default;

    auto poll(pin::Pin<mut_ref<WriteAllFuture>> self, task::Context& cx)
        -> task::Poll<Output> {
        auto& future = *self.get_unchecked_mut();
        if (future.completed) {
            rstd::panic { "async::io::WriteAllFuture polled after completion" };
        }

        while (future.written < future.buf->len()) {
            auto remaining = bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(
                future.buf->data() + future.written,
                future.buf->len() - future.written));
            auto out = poll_write(*future.writer, cx, remaining);
            if (out.is_pending()) {
                return task::Poll<Output>::Pending();
            }

            auto result = rstd::move(out).take();
            if (result.is_err()) {
                future.completed = true;
                return task::Poll<Output>::Ready(Err(rstd::move(result).unwrap_err_unchecked()));
            }

            auto n = rstd::move(result).unwrap_unchecked();
            if (n == 0) {
                future.completed = true;
                return task::Poll<Output>::Ready(Err(::rstd::io::error::Error_WRITE_ALL_EOF));
            }
            future.written += n;
        }

        future.completed = true;
        return task::Poll<Output>::Ready(Ok(empty {}));
    }
};

export template<AsyncWriteLike W>
class FlushFuture {
    W*   writer;
    bool completed { false };

public:
    using Output = ::rstd::io::Result<empty>;

    explicit FlushFuture(W& writer) : writer(rstd::addressof(writer)) {}

    FlushFuture(const FlushFuture&)            = delete;
    auto operator=(const FlushFuture&) -> FlushFuture& = delete;
    FlushFuture(FlushFuture&&) noexcept                    = default;
    auto operator=(FlushFuture&&) noexcept -> FlushFuture& = default;

    auto poll(pin::Pin<mut_ref<FlushFuture>> self, task::Context& cx) -> task::Poll<Output> {
        auto& future = *self.get_unchecked_mut();
        if (future.completed) {
            rstd::panic { "async::io::FlushFuture polled after completion" };
        }

        auto out = poll_flush(*future.writer, cx);
        if (out.is_ready()) {
            future.completed = true;
        }
        return out;
    }
};

export template<AsyncWriteLike W>
class ShutdownFuture {
    W*   writer;
    bool completed { false };

public:
    using Output = ::rstd::io::Result<empty>;

    explicit ShutdownFuture(W& writer) : writer(rstd::addressof(writer)) {}

    ShutdownFuture(const ShutdownFuture&)            = delete;
    auto operator=(const ShutdownFuture&) -> ShutdownFuture& = delete;
    ShutdownFuture(ShutdownFuture&&) noexcept                    = default;
    auto operator=(ShutdownFuture&&) noexcept -> ShutdownFuture& = default;

    auto poll(pin::Pin<mut_ref<ShutdownFuture>> self, task::Context& cx)
        -> task::Poll<Output> {
        auto& future = *self.get_unchecked_mut();
        if (future.completed) {
            rstd::panic { "async::io::ShutdownFuture polled after completion" };
        }

        auto out = poll_shutdown(*future.writer, cx);
        if (out.is_ready()) {
            future.completed = true;
        }
        return out;
    }
};

export template<AsyncReadLike R>
auto read(R& reader, bytes::BytesMut& buf) -> ReadFuture<R> {
    return ReadFuture<R> { reader, buf };
}

export template<AsyncWriteLike W>
auto write(W& writer, const bytes::Bytes& buf) -> WriteFuture<W> {
    return WriteFuture<W> { writer, buf };
}

export template<AsyncReadLike R>
auto read_exact(R& reader, bytes::BytesMut& buf, usize len) -> ReadExactFuture<R> {
    return ReadExactFuture<R> { reader, buf, len };
}

export template<AsyncWriteLike W>
auto write_all(W& writer, const bytes::Bytes& buf) -> WriteAllFuture<W> {
    return WriteAllFuture<W> { writer, buf };
}

export template<AsyncWriteLike W>
auto flush(W& writer) -> FlushFuture<W> {
    return FlushFuture<W> { writer };
}

export template<AsyncWriteLike W>
auto shutdown(W& writer) -> ShutdownFuture<W> {
    return ShutdownFuture<W> { writer };
}

} // namespace rstd::async::io
