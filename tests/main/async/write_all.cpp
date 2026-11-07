#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::bytes::Bytes;

namespace
{

enum class WriteMode
{
    Partial,
    Zero,
    Error,
    Pending
};

struct WriteProbe {
    const rstd::byte* base;
    rstd::slice<u8>   expected;
    usize             written {};
    usize             calls {};
    WriteMode         mode { WriteMode::Partial };

    auto poll_write(mut_ref<WriteProbe> self, rstd::task::Context&, const Bytes& input)
        -> rstd::task::Poll<rstd::io::Result<usize>> {
        using Poll = rstd::task::Poll<rstd::io::Result<usize>>;
        EXPECT_EQ(input.data(), self->base + self->written.to_primitive());
        EXPECT_EQ(input.len(), self->expected.len() - self->written);
        for (usize i {}; i < input.len(); ++i)
            EXPECT_EQ(input[i], self->expected[self->written + i]);
        ++self->calls;
        if (self->mode == WriteMode::Pending ||
            (self->mode == WriteMode::Partial && self->calls % usize(2) == usize(1)))
            return Poll::Pending();
        if (self->mode == WriteMode::Error)
            return Poll::Ready(Err(
                rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Other })));
        if (self->mode == WriteMode::Zero) return Poll::Ready(Ok(usize()));
        auto count = input.len() < usize(2) ? input.len() : usize(2);
        self->written += count;
        return Poll::Ready(Ok(count));
    }

    auto poll_flush(mut_ref<WriteProbe>, rstd::task::Context&)
        -> rstd::task::Poll<rstd::io::Result<empty>> {
        return rstd::task::Poll<rstd::io::Result<empty>>::Ready(Ok(empty {}));
    }
    auto poll_shutdown(mut_ref<WriteProbe>, rstd::task::Context&)
        -> rstd::task::Poll<rstd::io::Result<empty>> {
        return rstd::task::Poll<rstd::io::Result<empty>>::Ready(Ok(empty {}));
    }
};

static_assert(rstd::Impled<WriteProbe, rstd::async::io::AsyncWrite>);
static constexpr unsigned char STATIC_BYTES[] { 'x', 'a', 'b', 'c', 'd', 'e', 'f', 'y' };

void exercise_partial(Bytes bytes) {
    auto view   = bytes.slice(usize(1), usize(7));
    auto probe  = WriteProbe { view.data(), view.as_slice() };
    auto future = rstd::async::io::write_all(probe, view);
    auto cx     = rstd::task::Context { rstd::task::Waker::noop() };
    for (int i = 0; i != 3; ++i) {
        EXPECT_TRUE(rstd::future::poll(future, cx).is_pending());
    }
    auto result = rstd::future::poll(future, cx);
    ASSERT_TRUE(result.is_ready());
    EXPECT_TRUE(rstd::move(result).take().is_ok());
    EXPECT_EQ(probe.written, usize(6));
    EXPECT_EQ(probe.calls, usize(6));
}

} // namespace

TEST(AsyncWriteAll, ReusesVisibleStaticAndCustomAllocatedBytes) {
    exercise_partial(Bytes::from_static<STATIC_BYTES>());
    auto tracker = rstd::alloc::SharedTrackingAllocator<>::make();
    {
        auto bytes  = Bytes::copy_from_slice_in("xabcdefy"_bytes, tracker.clone());
        auto before = tracker.snapshot();
        exercise_partial(rstd::move(bytes));
        auto after = tracker.snapshot();
        EXPECT_EQ(after.allocations, before.allocations);
        EXPECT_EQ(after.grows, before.grows);
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(AsyncWriteAll, HandlesEmptyErrorsAndCancelledPendingWrite) {
    auto cx    = rstd::task::Context { rstd::task::Waker::noop() };
    auto empty = Bytes::make();
    auto probe = WriteProbe { empty.data(), empty.as_slice() };
    auto none  = rstd::async::io::write_all(probe, empty);
    auto done  = rstd::future::poll(none, cx);
    ASSERT_TRUE(done.is_ready());
    EXPECT_TRUE(rstd::move(done).take().is_ok());
    EXPECT_EQ(probe.calls, usize());

    auto      bytes = Bytes::copy_from_slice("abcdef"_bytes);
    WriteMode modes[] { WriteMode::Zero, WriteMode::Error, WriteMode::Pending };
    for (auto mode : modes) {
        auto writer = WriteProbe { bytes.data(), bytes.as_slice(), usize(), usize(), mode };
        auto future = rstd::async::io::write_all(writer, bytes);
        auto result = rstd::future::poll(future, cx);
        if (mode == WriteMode::Pending) {
            EXPECT_TRUE(result.is_pending());
        } else {
            ASSERT_TRUE(result.is_ready());
            auto error = rstd::move(result).take();
            ASSERT_TRUE(error.is_err());
            if (mode == WriteMode::Error)
                EXPECT_EQ(error.unwrap_err().kind(),
                          rstd::io::ErrorKind { rstd::io::ErrorKind::Other });
        }
        EXPECT_EQ(writer.written, usize());
        EXPECT_TRUE(bytes.is_unique());
    }
    EXPECT_EQ(bytes.as_slice(), "abcdef"_bytes);
}
