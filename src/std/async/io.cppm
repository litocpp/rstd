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

} // namespace rstd::async::io
