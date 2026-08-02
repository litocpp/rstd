#include <rstd/test/gtest.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
import rstd;
using namespace rstd;
using namespace rstd::literals;
using namespace rstd::sync::mpmc;

namespace
{

struct ChannelDropProbe {
    std::atomic<int>* drops;

    explicit ChannelDropProbe(std::atomic<int>& count): drops(&count) {}
    ChannelDropProbe(const ChannelDropProbe&)            = delete;
    ChannelDropProbe& operator=(const ChannelDropProbe&) = delete;
    ChannelDropProbe(ChannelDropProbe&& other) noexcept: drops(other.drops) {
        other.drops = nullptr;
    }
    ChannelDropProbe& operator=(ChannelDropProbe&& other) noexcept {
        if (this != &other) {
            if (drops) drops->fetch_add(1, std::memory_order_relaxed);
            drops       = other.drops;
            other.drops = nullptr;
        }
        return *this;
    }
    ~ChannelDropProbe() {
        if (drops) drops->fetch_add(1, std::memory_order_relaxed);
    }
};

void expect_multi_producer_multi_consumer(rstd::tuple<Sender<int>, Receiver<int>> endpoints,
                                          int messages_per_producer) {
    constexpr int producer_count = 4;
    constexpr int consumer_count = 4;
    int const     message_count  = producer_count * messages_per_producer;

    auto [sender, receiver] = rstd::move(endpoints);
    auto seen               = std::vector<std::atomic<int>>(message_count);
    for (auto& count : seen) count.store(0, std::memory_order_relaxed);

    auto invalid    = std::atomic<int> {};
    auto duplicates = std::atomic<int> {};
    auto failures   = std::atomic<int> {};
    auto consumers  = std::vector<std::thread> {};
    auto producers  = std::vector<std::thread> {};

    for (int i = 0; i < consumer_count; ++i) {
        consumers.emplace_back(
            [receiver = receiver, &seen, &invalid, &duplicates, message_count]() {
                while (true) {
                    auto result = receiver.recv();
                    if (result.is_err()) break;
                    int value = rstd::move(result).unwrap_unchecked();
                    if (value < 0 || value >= message_count) {
                        invalid.fetch_add(1, std::memory_order_relaxed);
                    } else if (seen[value].fetch_add(1, std::memory_order_relaxed) != 0) {
                        duplicates.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
    }

    for (int producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([sender = sender, producer, messages_per_producer, &failures]() {
            for (int sequence = 0; sequence < messages_per_producer; ++sequence) {
                if (sender.send(producer * messages_per_producer + sequence).is_err()) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    {
        auto last_sender = rstd::move(sender);
    }
    for (auto& producer : producers) producer.join();
    for (auto& consumer : consumers) consumer.join();

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(invalid.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(duplicates.load(std::memory_order_relaxed), 0);
    for (auto& count : seen) EXPECT_EQ(count.load(std::memory_order_relaxed), 1);
}

} // namespace

TEST(MpmcArray, BasicSendRecv) {
    auto [sender, receiver] = sync_channel<int>(usize(4));

    EXPECT_TRUE(sender.try_send(42).is_ok());
    EXPECT_TRUE(sender.try_send(100).is_ok());

    EXPECT_EQ(sender.len(), usize(2));
    EXPECT_FALSE(receiver.is_empty());
    EXPECT_FALSE(receiver.is_full());

    auto r1 = receiver.try_recv();
    ASSERT_TRUE(r1.is_ok());
    EXPECT_EQ(r1.unwrap_unchecked(), 42);

    auto r2 = receiver.try_recv();
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(r2.unwrap_unchecked(), 100);

    EXPECT_EQ(receiver.len(), usize());
    EXPECT_TRUE(sender.is_empty());
}

TEST(MpmcArray, FullLengthAndNonPowerOfTwoWrap) {
    auto [sender, receiver] = sync_channel<int>(usize(3));

    EXPECT_EQ(sender.capacity().unwrap_unchecked(), usize(3));
    EXPECT_TRUE(sender.try_send(0).is_ok());
    EXPECT_TRUE(sender.try_send(1).is_ok());
    EXPECT_TRUE(sender.try_send(2).is_ok());
    EXPECT_EQ(receiver.len(), usize(3));
    EXPECT_TRUE(sender.is_full());
    auto full = sender.try_send(3);
    ASSERT_TRUE(full.is_err());
    EXPECT_TRUE(full.unwrap_err_unchecked().is_full());

    EXPECT_EQ(receiver.try_recv().unwrap_unchecked(), 0);
    EXPECT_TRUE(sender.try_send(3).is_ok());
    EXPECT_EQ(sender.len(), usize(3));
    EXPECT_EQ(receiver.try_recv().unwrap_unchecked(), 1);
    EXPECT_EQ(receiver.try_recv().unwrap_unchecked(), 2);
    EXPECT_EQ(receiver.try_recv().unwrap_unchecked(), 3);
    EXPECT_TRUE(receiver.is_empty());
    EXPECT_EQ(sender.len(), usize());
}

TEST(MpmcList, LengthAcrossBlocks) {
    auto [sender, receiver]     = channel<int>();
    constexpr int message_count = 4'096;

    for (int i = 0; i < message_count; ++i) {
        EXPECT_TRUE(sender.try_send(i).is_ok());
    }
    EXPECT_EQ(receiver.len(), usize(message_count));

    for (int i = 0; i < message_count; ++i) {
        EXPECT_EQ(receiver.try_recv().unwrap_unchecked(), i);
    }
    EXPECT_TRUE(sender.capacity().is_none());
    EXPECT_TRUE(receiver.is_empty());
    EXPECT_EQ(sender.len(), usize());
}

TEST(Mpmc, SyncChannelBasic) {
    auto [tx, rx] = sync_channel<int>(usize(2));

    EXPECT_TRUE(tx.send(1).is_ok());
    EXPECT_TRUE(tx.send(2).is_ok());

    // Should block, but we test try_send
    EXPECT_TRUE(tx.try_send(3).is_err());

    EXPECT_EQ(rx.recv().unwrap_unchecked(), 1);
    EXPECT_TRUE(tx.send(3).is_ok());
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 2);
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 3);
}

TEST(Mpmc, SyncChannelThreads) {
    auto [tx, rx] = sync_channel<int>(usize(1));

    auto t1 = thread::spawn([tx = rstd::move(tx)]() mutable {
                  tx.send(1).unwrap_unchecked();
                  tx.send(2).unwrap_unchecked();
              }).unwrap_unchecked();

    EXPECT_EQ(rx.recv().unwrap_unchecked(), 1);
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 2);

    rstd::move(t1).join().unwrap_unchecked();
}

TEST(Mpmc, SyncChannelNonPowerOfTwoMultiProducerWrap) {
    constexpr int producer_count        = 4;
    constexpr int messages_per_producer = 2'048;
    constexpr int message_count         = producer_count * messages_per_producer;

    auto [tx, rx] = sync_channel<int>(usize(3));
    auto ready    = std::atomic<int> {};
    auto start    = std::atomic<bool> {};
    auto workers  = std::vector<std::thread> {};

    for (int producer = 0; producer < producer_count; ++producer) {
        workers.emplace_back([tx = tx, producer, &ready, &start]() mutable {
            ready.fetch_add(1, std::memory_order_release);
            while (! start.load(std::memory_order_acquire)) std::this_thread::yield();
            for (int sequence = 0; sequence < messages_per_producer; ++sequence) {
                tx.send(producer * messages_per_producer + sequence).unwrap_unchecked();
            }
        });
    }

    while (ready.load(std::memory_order_acquire) != producer_count) std::this_thread::yield();
    start.store(true, std::memory_order_release);

    auto seen = std::vector<bool>(message_count);
    for (int i = 0; i < message_count; ++i) {
        auto value = rx.recv().unwrap_unchecked();
        ASSERT_GE(value, 0);
        ASSERT_LT(value, message_count);
        ASSERT_FALSE(seen[value]);
        seen[value] = true;
    }
    for (auto& worker : workers) worker.join();
    {
        auto sender = rstd::move(tx);
    }
    EXPECT_TRUE(rx.recv().is_err());
}

TEST(Mpmc, SyncChannelBlockedSenderWakesAfterReceive) {
    auto [tx, rx] = sync_channel<int>(usize(1));
    tx.send(1).unwrap_unchecked();

    auto entered   = std::atomic<bool> {};
    auto completed = std::atomic<bool> {};
    auto worker    = std::thread([tx = tx, &entered, &completed]() mutable {
        entered.store(true, std::memory_order_release);
        completed.store(tx.send(2).is_ok(), std::memory_order_release);
    });

    while (! entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(completed.load(std::memory_order_acquire));
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 1);
    worker.join();
    EXPECT_TRUE(completed.load(std::memory_order_acquire));
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 2);
}

TEST(Mpmc, SyncChannelReceiverDisconnectWakesBlockedSender) {
    auto [tx, rx] = sync_channel<int>(usize(1));
    tx.send(1).unwrap_unchecked();

    auto entered        = std::atomic<bool> {};
    auto send_failed    = std::atomic<bool> {};
    auto returned_value = std::atomic<int> {};
    auto worker         = std::thread([tx = tx, &entered, &send_failed, &returned_value]() mutable {
        entered.store(true, std::memory_order_release);
        auto result = tx.send(2);
        if (result.is_err()) {
            returned_value.store(rstd::move(result).unwrap_err_unchecked().into_inner(),
                                 std::memory_order_release);
            send_failed.store(true, std::memory_order_release);
        }
    });

    while (! entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    {
        auto receiver = rstd::move(rx);
    }
    worker.join();
    EXPECT_TRUE(send_failed.load(std::memory_order_acquire));
    EXPECT_EQ(returned_value.load(std::memory_order_acquire), 2);
}

TEST(Mpmc, SyncChannelDisconnect) {
    auto [tx, rx] = sync_channel<int>(usize(1));

    {
        auto tx2 = tx;
        tx2.send(1).unwrap_unchecked();
    } // tx2 dropped

    EXPECT_EQ(rx.recv().unwrap_unchecked(), 1);

    auto t1 = thread::spawn([tx = rstd::move(tx)]() mutable {
                  thread::sleep(rstd::time::Duration::from_millis(u64(100)));
                  // tx will be dropped after this lambda exits
              }).unwrap_unchecked();

    // rx should eventually see disconnect
    EXPECT_TRUE(rx.recv().is_err());
}

TEST(Mpmc, UnboundedChannel) {
    auto [tx, rx] = channel<int>();

    EXPECT_TRUE(tx.send(1).is_ok());
    EXPECT_TRUE(tx.send(2).is_ok());
    EXPECT_TRUE(tx.send(3).is_ok());

    EXPECT_EQ(rx.recv().unwrap_unchecked(), 1);
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 2);
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 3);
}

TEST(Mpmc, UnboundedChannelThreads) {
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

TEST(Mpmc, UnboundedChannelCrossBlockFifo) {
    auto [tx, rx]               = channel<int>();
    constexpr int message_count = 4'096;

    for (int i = 0; i < message_count; ++i) tx.send(i).unwrap_unchecked();
    for (int i = 0; i < message_count; ++i) {
        EXPECT_EQ(rx.recv().unwrap_unchecked(), i);
    }
}

TEST(Mpmc, UnboundedChannelMultiProducerAcrossBlocks) {
    constexpr int producer_count        = 8;
    constexpr int messages_per_producer = 4'096;
    constexpr int message_count         = producer_count * messages_per_producer;

    auto [tx, rx] = channel<int>();
    auto ready    = std::atomic<int> {};
    auto start    = std::atomic<bool> {};
    auto workers  = std::vector<std::thread> {};

    for (int producer = 0; producer < producer_count; ++producer) {
        workers.emplace_back([tx = tx, producer, &ready, &start]() mutable {
            ready.fetch_add(1, std::memory_order_release);
            while (! start.load(std::memory_order_acquire)) std::this_thread::yield();

            for (int sequence = 0; sequence < messages_per_producer; ++sequence) {
                tx.send(producer * messages_per_producer + sequence).unwrap_unchecked();
            }
        });
    }

    while (ready.load(std::memory_order_acquire) != producer_count) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    {
        auto sender = rstd::move(tx);
    }

    auto seen = std::vector<bool>(message_count);
    for (int i = 0; i < message_count; ++i) {
        auto received = rx.recv();
        ASSERT_TRUE(received.is_ok());
        auto value = received.unwrap_unchecked();
        ASSERT_GE(value, 0);
        ASSERT_LT(value, message_count);
        ASSERT_FALSE(seen[value]);
        seen[value] = true;
    }

    EXPECT_TRUE(rx.recv().is_err());
}

// Regression: ListChannel::start_recv must detect disconnect when the
// channel is empty (head == tail). Earlier the unmasked compare against
// head_idx missed the disconnect bit set by `tail |= 1`, so a receiver
// that drained the queue and re-entered start_recv would spin forever.
TEST(Mpmc, UnboundedDisconnectAfterDrain) {
    auto [tx, rx] = channel<int>();
    EXPECT_TRUE(tx.send(1).is_ok());
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 1);

    auto t = thread::spawn([rx = rstd::move(rx)]() mutable {
                 // Channel is empty here; senders disconnected below.
                 // recv() must return Err, not spin.
                 EXPECT_TRUE(rx.recv().is_err());
             }).unwrap_unchecked();

    // Give the receiver a moment to enter start_recv before disconnecting.
    rstd::thread::sleep(rstd::time::Duration::from_millis(u64(50)));
    {
        auto _ = rstd::move(tx);
    } // drop sender, triggers disconnect

    rstd::move(t).join().unwrap_unchecked();
}

TEST(Mpmc, UnboundedReceiverDropDestroysPendingMessages) {
    auto drops    = std::atomic<int> {};
    auto [tx, rx] = channel<ChannelDropProbe>();

    for (int i = 0; i < 100; ++i) {
        tx.send(ChannelDropProbe { drops }).unwrap_unchecked();
    }
    {
        auto receiver = rstd::move(rx);
    }
    EXPECT_EQ(drops.load(std::memory_order_relaxed), 100);
    EXPECT_TRUE(tx.send(ChannelDropProbe { drops }).is_err());
    EXPECT_EQ(drops.load(std::memory_order_relaxed), 101);
}

TEST(Mpmc, UnboundedSenderDisconnectBeforeReceiverDropDestroysPendingMessages) {
    auto drops    = std::atomic<int> {};
    auto [tx, rx] = channel<ChannelDropProbe>();

    for (int i = 0; i < 100; ++i) {
        tx.send(ChannelDropProbe { drops }).unwrap_unchecked();
    }
    {
        auto sender = rstd::move(tx);
    }
    EXPECT_EQ(drops.load(std::memory_order_relaxed), 0);
    {
        auto receiver = rstd::move(rx);
    }
    EXPECT_EQ(drops.load(std::memory_order_relaxed), 100);
}

TEST(Mpmc, SyncReceiverDropDestroysPendingMessages) {
    auto drops    = std::atomic<int> {};
    auto [tx, rx] = sync_channel<ChannelDropProbe>(usize(3));

    for (int i = 0; i < 3; ++i) {
        tx.send(ChannelDropProbe { drops }).unwrap_unchecked();
    }
    {
        auto receiver = rstd::move(rx);
    }
    EXPECT_EQ(drops.load(std::memory_order_relaxed), 3);
    EXPECT_TRUE(tx.send(ChannelDropProbe { drops }).is_err());
    EXPECT_EQ(drops.load(std::memory_order_relaxed), 4);
}

TEST(Mpmc, SyncSenderDisconnectBeforeReceiverDropDestroysPendingMessages) {
    auto drops    = std::atomic<int> {};
    auto [tx, rx] = sync_channel<ChannelDropProbe>(usize(3));

    for (int i = 0; i < 3; ++i) {
        tx.send(ChannelDropProbe { drops }).unwrap_unchecked();
    }
    {
        auto sender = rstd::move(tx);
    }
    EXPECT_EQ(drops.load(std::memory_order_relaxed), 0);
    {
        auto receiver = rstd::move(rx);
    }
    EXPECT_EQ(drops.load(std::memory_order_relaxed), 3);
}

TEST(Mpmc, ZeroChannelSendWaitsForReceive) {
    auto [tx, rx]  = sync_channel<int>(usize());
    auto entered   = std::atomic<bool> {};
    auto completed = std::atomic<bool> {};
    auto worker    = std::thread([tx = tx, &entered, &completed]() mutable {
        entered.store(true, std::memory_order_release);
        completed.store(tx.send(42).is_ok(), std::memory_order_release);
    });

    while (! entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(completed.load(std::memory_order_acquire));
    EXPECT_EQ(rx.recv().unwrap_unchecked(), 42);
    worker.join();
    EXPECT_TRUE(completed.load(std::memory_order_acquire));
}

TEST(Mpmc, ZeroChannelReceiveWaitsForSend) {
    auto [tx, rx]  = sync_channel<int>(usize());
    auto entered   = std::atomic<bool> {};
    auto completed = std::atomic<bool> {};
    auto value     = std::atomic<int> {};
    auto worker    = std::thread([rx = rstd::move(rx), &entered, &completed, &value]() mutable {
        entered.store(true, std::memory_order_release);
        auto result = rx.recv();
        if (result.is_ok()) {
            value.store(result.unwrap_unchecked(), std::memory_order_release);
            completed.store(true, std::memory_order_release);
        }
    });

    while (! entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(completed.load(std::memory_order_acquire));
    EXPECT_TRUE(tx.send(7).is_ok());
    worker.join();
    EXPECT_TRUE(completed.load(std::memory_order_acquire));
    EXPECT_EQ(value.load(std::memory_order_acquire), 7);
}

TEST(Mpmc, ZeroChannelTryOperationsRequireWaitingPeer) {
    {
        auto [tx, rx] = sync_channel<int>(usize());
        EXPECT_TRUE(tx.try_send(1).is_err());

        auto entered = std::atomic<bool> {};
        auto value   = std::atomic<int> {};
        auto worker  = std::thread([rx = rstd::move(rx), &entered, &value]() mutable {
            entered.store(true, std::memory_order_release);
            auto result = rx.recv();
            if (result.is_ok()) value.store(result.unwrap_unchecked(), std::memory_order_release);
        });
        while (! entered.load(std::memory_order_acquire)) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_TRUE(tx.try_send(2).is_ok());
        worker.join();
        EXPECT_EQ(value.load(std::memory_order_acquire), 2);
    }

    {
        auto [tx, rx] = sync_channel<int>(usize());
        EXPECT_TRUE(rx.try_recv().is_err());

        auto entered   = std::atomic<bool> {};
        auto completed = std::atomic<bool> {};
        auto worker    = std::thread([tx = tx, &entered, &completed]() mutable {
            entered.store(true, std::memory_order_release);
            completed.store(tx.send(3).is_ok(), std::memory_order_release);
        });
        while (! entered.load(std::memory_order_acquire)) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_EQ(rx.try_recv().unwrap_unchecked(), 3);
        worker.join();
        EXPECT_TRUE(completed.load(std::memory_order_acquire));
    }
}

TEST(Mpmc, ZeroChannelDisconnectWakesWaitingReceiver) {
    auto [tx, rx] = sync_channel<int>(usize());
    auto entered  = std::atomic<bool> {};
    auto failed   = std::atomic<bool> {};
    auto worker   = std::thread([rx = rstd::move(rx), &entered, &failed]() mutable {
        entered.store(true, std::memory_order_release);
        failed.store(rx.recv().is_err(), std::memory_order_release);
    });

    while (! entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    {
        auto sender = rstd::move(tx);
    }
    worker.join();
    EXPECT_TRUE(failed.load(std::memory_order_acquire));
}

TEST(Mpmc, ZeroChannelDisconnectReturnsWaitingMessage) {
    auto [tx, rx]       = sync_channel<int>(usize());
    auto entered        = std::atomic<bool> {};
    auto send_failed    = std::atomic<bool> {};
    auto returned_value = std::atomic<int> {};
    auto worker         = std::thread([tx = tx, &entered, &send_failed, &returned_value]() mutable {
        entered.store(true, std::memory_order_release);
        auto result = tx.send(9);
        if (result.is_err()) {
            returned_value.store(rstd::move(result).unwrap_err_unchecked().into_inner(),
                                 std::memory_order_release);
            send_failed.store(true, std::memory_order_release);
        }
    });

    while (! entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    {
        auto receiver = rstd::move(rx);
    }
    worker.join();
    EXPECT_TRUE(send_failed.load(std::memory_order_acquire));
    EXPECT_EQ(returned_value.load(std::memory_order_acquire), 9);
}

TEST(Mpmc, ZeroChannelDisconnectDestroysWaitingMessageOnce) {
    auto drops    = std::atomic<int> {};
    auto [tx, rx] = sync_channel<ChannelDropProbe>(usize());
    auto entered  = std::atomic<bool> {};
    auto failed   = std::atomic<bool> {};
    auto worker   = std::thread([tx = tx, &drops, &entered, &failed]() mutable {
        entered.store(true, std::memory_order_release);
        failed.store(tx.send(ChannelDropProbe { drops }).is_err(), std::memory_order_release);
    });

    while (! entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    {
        auto receiver = rstd::move(rx);
    }
    worker.join();
    EXPECT_TRUE(failed.load(std::memory_order_acquire));
    EXPECT_EQ(drops.load(std::memory_order_relaxed), 1);
}

TEST(Mpmc, ZeroChannelMultiProducerRendezvous) {
    constexpr int producer_count        = 4;
    constexpr int messages_per_producer = 256;
    constexpr int message_count         = producer_count * messages_per_producer;

    auto [tx, rx] = sync_channel<int>(usize());
    auto workers  = std::vector<std::thread> {};
    for (int producer = 0; producer < producer_count; ++producer) {
        workers.emplace_back([tx = tx, producer]() mutable {
            for (int sequence = 0; sequence < messages_per_producer; ++sequence) {
                tx.send(producer * messages_per_producer + sequence).unwrap_unchecked();
            }
        });
    }

    auto seen = std::vector<bool>(message_count);
    for (int i = 0; i < message_count; ++i) {
        auto value = rx.recv().unwrap_unchecked();
        ASSERT_GE(value, 0);
        ASSERT_LT(value, message_count);
        ASSERT_FALSE(seen[value]);
        seen[value] = true;
    }
    for (auto& worker : workers) worker.join();
}

TEST(Mpmc, SenderMoveAssignmentReleasesOldChannel) {
    auto [first_tx, first_rx]   = channel<int>();
    auto [second_tx, second_rx] = channel<int>();

    first_tx = rstd::move(second_tx);
    EXPECT_TRUE(first_rx.recv().is_err());
    EXPECT_TRUE(first_tx.send(11).is_ok());
    EXPECT_EQ(second_rx.recv().unwrap_unchecked(), 11);
}

TEST(Mpmc, ReceiverMoveAssignmentReleasesOldChannel) {
    auto [first_tx, first_rx]   = channel<int>();
    auto [second_tx, second_rx] = channel<int>();

    first_rx = rstd::move(second_rx);
    EXPECT_TRUE(first_tx.send(1).is_err());
    EXPECT_TRUE(second_tx.send(12).is_ok());
    EXPECT_EQ(first_rx.recv().unwrap_unchecked(), 12);
}

TEST(Mpmc, SenderMoveAssignmentChangesFlavorSafely) {
    auto [array_tx, array_rx] = sync_channel<int>(usize(1));
    auto [zero_tx, zero_rx]   = sync_channel<int>(usize());

    array_tx = rstd::move(zero_tx);
    EXPECT_TRUE(array_rx.recv().is_err());

    auto completed = std::atomic<bool> {};
    auto worker    = std::thread([tx = array_tx, &completed]() mutable {
        completed.store(tx.send(13).is_ok(), std::memory_order_release);
    });
    EXPECT_EQ(zero_rx.recv().unwrap_unchecked(), 13);
    worker.join();
    EXPECT_TRUE(completed.load(std::memory_order_acquire));
}

TEST(Mpmc, ReceiverCloneKeepsChannelConnectedUntilLastReceiverDrops) {
    auto [sender, receiver] = channel<int>();
    auto receiver_clone     = receiver;

    {
        auto original = rstd::move(receiver);
    }
    EXPECT_TRUE(sender.send(17).is_ok());
    EXPECT_EQ(receiver_clone.recv().unwrap_unchecked(), 17);

    {
        auto last_receiver = rstd::move(receiver_clone);
    }
    auto result = sender.send(18);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err_unchecked().into_inner(), 18);
}

TEST(Mpmc, SenderCopyAssignmentReleasesOldChannelAndChangesFlavor) {
    auto [list_sender, list_receiver]   = channel<int>();
    auto [array_sender, array_receiver] = sync_channel<int>(usize(1));

    list_sender = array_sender;
    EXPECT_TRUE(list_receiver.recv().is_err());
    EXPECT_TRUE(list_sender.same_channel(array_sender));
    list_sender.send(19).unwrap_unchecked();
    EXPECT_EQ(array_receiver.recv().unwrap_unchecked(), 19);
}

TEST(Mpmc, ReceiverCopyAssignmentReleasesOldChannelAndChangesFlavor) {
    auto [list_sender, list_receiver] = channel<int>();
    auto [zero_sender, zero_receiver] = sync_channel<int>(usize());

    list_receiver = zero_receiver;
    EXPECT_TRUE(list_sender.send(20).is_err());
    EXPECT_TRUE(list_receiver.same_channel(zero_receiver));

    auto worker = std::thread([zero_sender] {
        zero_sender.send(21).unwrap_unchecked();
    });
    EXPECT_EQ(list_receiver.recv().unwrap_unchecked(), 21);
    worker.join();
}

TEST(Mpmc, ListMultiProducerMultiConsumer) {
    expect_multi_producer_multi_consumer(channel<int>(), 2'048);
}

TEST(Mpmc, ArrayMultiProducerMultiConsumer) {
    expect_multi_producer_multi_consumer(sync_channel<int>(usize(3)), 2'048);
}

TEST(Mpmc, ZeroMultiProducerMultiConsumer) {
    expect_multi_producer_multi_consumer(sync_channel<int>(usize()), 256);
}

TEST(Mpmc, TryErrorsDistinguishEmptyFullAndDisconnected) {
    auto [sender, receiver] = sync_channel<int>(usize(1));

    auto empty_result = receiver.try_recv();
    ASSERT_TRUE(empty_result.is_err());
    EXPECT_EQ(empty_result.unwrap_err_unchecked(), TryRecvError::Empty);

    sender.try_send(1).unwrap_unchecked();
    auto full_result = sender.try_send(2);
    ASSERT_TRUE(full_result.is_err());
    auto full_error = rstd::move(full_result).unwrap_err_unchecked();
    EXPECT_TRUE(full_error.is_full());
    EXPECT_EQ(rstd::move(full_error).into_inner(), 2);

    {
        auto last_receiver = rstd::move(receiver);
    }
    auto disconnected_send = sender.try_send(3);
    ASSERT_TRUE(disconnected_send.is_err());
    auto send_error = rstd::move(disconnected_send).unwrap_err_unchecked();
    EXPECT_TRUE(send_error.is_disconnected());
    EXPECT_EQ(rstd::move(send_error).into_inner(), 3);

    auto [other_sender, other_receiver] = channel<int>();
    {
        auto last_sender = rstd::move(other_sender);
    }
    auto disconnected_recv = other_receiver.try_recv();
    ASSERT_TRUE(disconnected_recv.is_err());
    EXPECT_EQ(disconnected_recv.unwrap_err_unchecked(), TryRecvError::Disconnected);
}

TEST(Mpmc, ErrorFormattingMatchesRustStd) {
    static_assert(Impled<RecvError, fmt::Display>);
    static_assert(Impled<RecvError, fmt::Debug>);
    static_assert(Impled<SendError<int>, error::Error>);
    static_assert(Impled<TrySendError<int>, error::Error>);
    static_assert(Impled<SendTimeoutError<int>, error::Error>);
    static_assert(Impled<RecvError, error::Error>);
    static_assert(Impled<TryRecvError, error::Error>);
    static_assert(Impled<RecvTimeoutError, error::Error>);

    EXPECT_EQ(rstd::format("{}", RecvError {}), "receiving on a closed channel"_str);
    EXPECT_EQ(rstd::format("{:?}", TryRecvError::Empty), "Empty"_str);
    EXPECT_EQ(rstd::format("{}", RecvTimeoutError::Timeout), "timed out waiting on channel"_str);

    auto full = TrySendError<int>::Full(1);
    EXPECT_EQ(rstd::format("{}", full), "sending on a full channel"_str);
    EXPECT_EQ(rstd::format("{:?}", full), "Full(..)"_str);

    auto timeout = SendTimeoutError<int>::Timeout(2);
    EXPECT_EQ(rstd::format("{}", timeout), "timed out waiting on send operation"_str);
    EXPECT_EQ(rstd::format("{:?}", timeout), "SendTimeoutError(..)"_str);
}

TEST(Mpmc, ZeroTrySendDistinguishesFullAndDisconnected) {
    auto [sender, receiver] = sync_channel<int>(usize());

    auto full_result = sender.try_send(4);
    ASSERT_TRUE(full_result.is_err());
    EXPECT_TRUE(full_result.unwrap_err_unchecked().is_full());

    {
        auto last_receiver = rstd::move(receiver);
    }
    auto disconnected_result = sender.try_send(5);
    ASSERT_TRUE(disconnected_result.is_err());
    EXPECT_TRUE(disconnected_result.unwrap_err_unchecked().is_disconnected());
}

TEST(Mpmc, SendTimeoutReturnsMessageForArrayAndZero) {
    {
        auto [sender, receiver] = sync_channel<int>(usize(1));
        sender.send(1).unwrap_unchecked();
        auto result = sender.send_timeout(2, time::Duration::from_millis(u64(5)));
        ASSERT_TRUE(result.is_err());
        auto error = rstd::move(result).unwrap_err_unchecked();
        EXPECT_TRUE(error.is_timeout());
        EXPECT_EQ(rstd::move(error).into_inner(), 2);
    }

    {
        auto [sender, receiver] = sync_channel<int>(usize());
        auto result             = sender.send_timeout(3, time::Duration::from_millis(u64(5)));
        ASSERT_TRUE(result.is_err());
        auto error = rstd::move(result).unwrap_err_unchecked();
        EXPECT_TRUE(error.is_timeout());
        EXPECT_EQ(rstd::move(error).into_inner(), 3);
    }
}

TEST(Mpmc, ReceiveTimeoutDistinguishesTimeoutAndDisconnect) {
    {
        auto [sender, receiver] = channel<int>();
        auto result             = receiver.recv_timeout(time::Duration::from_millis(u64(5)));
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err_unchecked(), RecvTimeoutError::Timeout);
    }

    {
        auto [sender, receiver] = sync_channel<int>(usize());
        auto result             = receiver.recv_timeout(time::Duration::from_millis(u64(5)));
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err_unchecked(), RecvTimeoutError::Timeout);
    }

    {
        auto [sender, receiver] = channel<int>();
        {
            auto last_sender = rstd::move(sender);
        }
        auto result = receiver.recv_timeout(time::Duration::from_millis(u64(100)));
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err_unchecked(), RecvTimeoutError::Disconnected);
    }
}

TEST(Mpmc, DeadlineOperationsSucceedWhenPeerBecomesReady) {
    auto [sender, receiver] = sync_channel<int>(usize());
    auto worker             = std::thread([sender = sender] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        sender.send_deadline(31, time::Instant::now() + time::Duration::from_millis(u64(100)))
            .unwrap_unchecked();
    });

    auto result =
        receiver.recv_deadline(time::Instant::now() + time::Duration::from_millis(u64(100)));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap_unchecked(), 31);
    worker.join();
}

TEST(Mpmc, StateQueriesAndSameChannelCoverAllFlavors) {
    auto [list_sender, list_receiver] = channel<int>();
    auto list_sender_clone            = list_sender;
    auto list_receiver_clone          = list_receiver;
    auto [other_sender, other_recv]   = channel<int>();

    EXPECT_TRUE(list_sender.same_channel(list_sender_clone));
    EXPECT_TRUE(list_receiver.same_channel(list_receiver_clone));
    EXPECT_FALSE(list_sender.same_channel(other_sender));
    EXPECT_FALSE(list_receiver.same_channel(other_recv));
    EXPECT_TRUE(list_sender.capacity().is_none());
    EXPECT_TRUE(list_receiver.is_empty());
    EXPECT_FALSE(list_sender.is_full());

    auto [array_sender, array_receiver] = sync_channel<int>(usize(2));
    EXPECT_EQ(array_sender.capacity().unwrap_unchecked(), usize(2));
    EXPECT_EQ(array_receiver.capacity().unwrap_unchecked(), usize(2));
    EXPECT_TRUE(array_sender.is_empty());
    array_sender.send(1).unwrap_unchecked();
    array_sender.send(2).unwrap_unchecked();
    EXPECT_EQ(array_receiver.len(), usize(2));
    EXPECT_TRUE(array_receiver.is_full());

    auto [zero_sender, zero_receiver] = sync_channel<int>(usize());
    EXPECT_EQ(zero_sender.capacity().unwrap_unchecked(), usize());
    EXPECT_EQ(zero_receiver.len(), usize());
    EXPECT_TRUE(zero_sender.is_empty());
    EXPECT_TRUE(zero_receiver.is_full());
}

TEST(Mpmc, ReceiverIteratorsFollowBlockingAndOwningSemantics) {
    {
        auto [sender, receiver] = channel<int>();
        sender.send(1).unwrap_unchecked();
        sender.send(2).unwrap_unchecked();
        auto iterator = receiver.try_iter();
        EXPECT_EQ(iterator.next().unwrap_unchecked(), 1);
        EXPECT_EQ(iterator.next().unwrap_unchecked(), 2);
        EXPECT_TRUE(iterator.next().is_none());
    }

    {
        auto [sender, receiver] = channel<int>();
        sender.send(3).unwrap_unchecked();
        {
            auto last_sender = rstd::move(sender);
        }
        auto iterator = receiver.iter();
        EXPECT_EQ(iterator.next().unwrap_unchecked(), 3);
        EXPECT_TRUE(iterator.next().is_none());
    }

    {
        auto [sender, receiver] = channel<int>();
        sender.send(4).unwrap_unchecked();
        {
            auto last_sender = rstd::move(sender);
        }
        auto iterator = rstd::move(receiver).into_iter();
        EXPECT_EQ(iterator.next().unwrap_unchecked(), 4);
        EXPECT_TRUE(iterator.next().is_none());
    }
}
