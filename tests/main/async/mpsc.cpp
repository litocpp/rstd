#include <rstd/test/gtest.hpp>
#include <atomic>
import rstd;
using namespace rstd;
using namespace rstd::prelude;

namespace
{
TEST(AsyncMpsc, TrySendPreservesFullValueAndReusesCapacity) {
    auto channel  = async::mpsc::channel<Box<int>>(usize(2));
    auto sender   = rstd::move(channel.get<0>());
    auto receiver = rstd::move(channel.get<1>());
    EXPECT_TRUE(sender.try_send(Box<int>::make(1)).is_ok());
    EXPECT_TRUE(sender.try_send(Box<int>::make(2)).is_ok());
    auto full = sender.try_send(Box<int>::make(3));
    ASSERT_TRUE(full.is_err());
    EXPECT_TRUE(full.as_ref().unwrap_err().is_full());
    auto rejected = rstd::move(full).unwrap_err().into_inner();
    EXPECT_EQ(*rejected, 3);
    EXPECT_EQ(**async::block_on(receiver.recv()), 1);
    EXPECT_TRUE(sender.try_send(rstd::move(rejected)).is_ok());
    receiver.close();
    EXPECT_EQ(**async::block_on(receiver.recv()), 2);
    EXPECT_EQ(**async::block_on(receiver.recv()), 3);
    EXPECT_TRUE(async::block_on(receiver.recv()).is_none());
}

TEST(AsyncMpsc, TrySendDoesNotStealQueuedSenderReservation) {
    auto channel  = async::mpsc::channel<int>(usize(1));
    auto sender   = rstd::move(channel.get<0>());
    auto receiver = rstd::move(channel.get<1>());
    EXPECT_TRUE(sender.try_send(1).is_ok());
    auto waiting = async::into_future(sender.send(2));
    auto cx      = task::Context { task::Waker::noop() };
    EXPECT_TRUE(future::poll(waiting, cx).is_pending());
    EXPECT_EQ(*async::block_on(receiver.recv()), 1);
    auto full = sender.try_send(3);
    ASSERT_TRUE(full.is_err());
    EXPECT_TRUE(full.as_ref().unwrap_err().is_full());
    auto sent = future::poll(waiting, cx);
    ASSERT_TRUE(sent.is_ready());
    EXPECT_TRUE(rstd::move(sent).take().is_ok());
    EXPECT_EQ(*async::block_on(receiver.recv()), 2);
    EXPECT_TRUE(sender.try_send(3).is_ok());
    EXPECT_EQ(*async::block_on(receiver.recv()), 3);
}

TEST(AsyncMpsc, CloseRejectsNewSendsWakesSenderAndDrainsQueue) {
    auto channel  = async::mpsc::channel<int>(usize(1));
    auto sender   = rstd::move(channel.get<0>());
    auto receiver = rstd::move(channel.get<1>());
    EXPECT_TRUE(sender.try_send(1).is_ok());
    auto waiting = async::into_future(sender.send(2));
    auto cx      = task::Context { task::Waker::noop() };
    EXPECT_TRUE(future::poll(waiting, cx).is_pending());
    receiver.close();
    receiver.close();
    EXPECT_TRUE(sender.is_closed());
    auto rejected = sender.try_send(3);
    ASSERT_TRUE(rejected.is_err());
    EXPECT_TRUE(rejected.as_ref().unwrap_err().is_disconnected());
    EXPECT_EQ(rstd::move(rejected).unwrap_err().into_inner(), 3);
    auto sent = future::poll(waiting, cx);
    ASSERT_TRUE(sent.is_ready());
    auto result = rstd::move(sent).take();
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err().into_inner(), 2);
    EXPECT_EQ(*async::block_on(receiver.recv()), 1);
    EXPECT_TRUE(async::block_on(receiver.recv()).is_none());
}

TEST(AsyncMpsc, CloseWakesAnEmptyReceiver) {
    auto channel  = async::mpsc::channel<int>(usize(1));
    auto sender   = rstd::move(channel.get<0>());
    auto receiver = rstd::move(channel.get<1>());
    auto waiting  = async::into_future(receiver.recv());
    auto cx       = task::Context { task::Waker::noop() };
    EXPECT_TRUE(future::poll(waiting, cx).is_pending());
    receiver.close();
    auto received = future::poll(waiting, cx);
    ASSERT_TRUE(received.is_ready());
    EXPECT_TRUE(rstd::move(received).take().is_none());
    EXPECT_TRUE(sender.is_closed());
}

TEST(AsyncMpsc, ReceiverDropReturnsDisconnectedValue) {
    auto sender = [] {
        auto channel = async::mpsc::channel<int>(usize(1));
        return rstd::move(channel.get<0>());
    }();
    auto result = sender.try_send(7);
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(result.as_ref().unwrap_err().is_disconnected());
    EXPECT_EQ(rstd::move(result).unwrap_err().into_inner(), 7);
}

auto receive_after_entered(async::mpsc::Receiver<int> receiver, std::atomic<bool>& entered)
    -> async::coro<Option<int>> {
    entered.store(true, std::memory_order_release);
    co_return co_await receiver.recv();
}
TEST(AsyncMpsc, TrySendFromAnotherThreadWakesReceiver) {
    auto channel  = async::mpsc::channel<int>(usize(1));
    auto entered  = std::atomic<bool> { false };
    auto producer = thread::spawn([sender = rstd::move(channel.get<0>()), &entered] {
                        while (! entered.load(std::memory_order_acquire)) hint::spin_loop();
                        thread::sleep(time::Duration::from_millis(u64(1)));
                        return sender.try_send(9).is_ok();
                    }).unwrap();
    auto runtime  = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto result   = runtime.block_on(async::select(
        async::into_future(receive_after_entered(rstd::move(channel.get<1>()), entered)),
        async::sleep(time::Duration::from_millis(u64(500)))));
    ASSERT_TRUE(result.is_left());
    auto received = rstd::move(result).unwrap_left();
    ASSERT_TRUE(received.is_some());
    EXPECT_EQ(*received, 9);
    EXPECT_TRUE(rstd::move(producer).join().unwrap());
}
} // namespace
