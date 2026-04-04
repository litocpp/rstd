#include <gtest/gtest.h>
import rstd;
using namespace rstd;
using namespace rstd::sync::mpsc;


TEST(MpmcArray, BasicSendRecv) {
    auto channel = mpmc::Channel<int>::with_capacity(4);

    EXPECT_TRUE(channel->try_send(42));
    EXPECT_TRUE(channel->try_send(100));

    EXPECT_EQ(channel->len(), 2);
    EXPECT_FALSE(channel->is_empty());
    EXPECT_FALSE(channel->is_full());

    auto r1 = channel->try_recv();
    ASSERT_TRUE(r1.is_ok());
    EXPECT_EQ(r1.unwrap_unchecked(), 42);

    auto r2 = channel->try_recv();
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(r2.unwrap_unchecked(), 100);

    EXPECT_EQ(channel->len(), 0);
    EXPECT_TRUE(channel->is_empty());
}

TEST(Mpsc, SyncChannelBasic) {
    auto [tx, rx] = sync_channel<int>(2);

    EXPECT_TRUE(tx.send(1).is_ok());
    EXPECT_TRUE(tx.send(2).is_ok());

    // Should block, but we test try_send
    EXPECT_TRUE(tx.try_send(3).is_err());

    EXPECT_EQ(rx.recv().unwrap_unchecked(), 1);
    EXPECT_TRUE(tx.send(3).is_ok());
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 2);
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 3);
}

TEST(Mpsc, SyncChannelThreads) {
    auto [tx, rx] = sync_channel<int>(1);

    auto t1 = thread::spawn([tx = rstd::move(tx)]() mutable {
                  tx.send(1).unwrap_unchecked();
                  tx.send(2).unwrap_unchecked();
              }).unwrap_unchecked();

    EXPECT_EQ(rx.recv().unwrap_unchecked(), 1);
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 2);

    rstd::move(t1).join().unwrap_unchecked();
}

TEST(Mpsc, SyncChannelDisconnect) {
    auto [tx, rx] = sync_channel<int>(1);

    {
        auto tx2 = tx;
        tx2.send(1).unwrap_unchecked();
    } // tx2 dropped

    EXPECT_EQ(rx.recv().unwrap_unchecked(), 1);

    auto t1 = thread::spawn([tx = rstd::move(tx)]() mutable {
                  thread::sleep(rstd::time::Duration::from_millis(100));
                  // tx will be dropped after this lambda exits
              }).unwrap_unchecked();

    // rx should eventually see disconnect
    EXPECT_TRUE(rx.recv().is_err());
}

TEST(Mpsc, UnboundedChannel) {
    auto [tx, rx] = channel<int>();

    EXPECT_TRUE(tx.send(1).is_ok());
    EXPECT_TRUE(tx.send(2).is_ok());
    EXPECT_TRUE(tx.send(3).is_ok());

    EXPECT_EQ(rx.recv().unwrap_unchecked(), 1);
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 2);
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 3);
}

TEST(Mpsc, UnboundedChannelThreads) {
    auto [tx, rx] = channel<int>();

    auto t1 = thread::spawn([tx = tx]() mutable {
                  tx.send(10).unwrap_unchecked();
              }).unwrap_unchecked();

    auto t2 = thread::spawn([tx = tx]() mutable {
                  tx.send(20).unwrap_unchecked();
              }).unwrap_unchecked();

    auto v1 = rx.recv().unwrap_unchecked();
    auto v2 = rx.recv().unwrap_unchecked();

    EXPECT_TRUE((v1 == 10 && v2 == 20) || (v1 == 20 && v2 == 10));

    rstd::move(t1).join().unwrap_unchecked();
    rstd::move(t2).join().unwrap_unchecked();
}
