module;

#include <atomic>
#include <memory>

export module mpmc:counter;

namespace mpmc::counter
{
template<typename C>
struct Counter {
    std::atomic_size_t senders;
    std::atomic_size_t receivers;

    // mark last
    std::atomic_bool   destroy;
    std::unique_ptr<C> chan;
};

export template<typename C>
struct Sender {
    Counter<C>* counter_ptr { nullptr };

    auto counter() const -> Counter<C>& { return *counter_ptr; }

    auto acquire() const -> Sender<C> {
        auto count = counter().senders.fetch_add(1, std::memory_order_relaxed);
        return Sender<C> { counter_ptr };
    }

    auto release(void (*disconnect)(C& q)) -> void {
        if (counter().senders.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            disconnect(*(counter().chan));

            if (counter().destroy.exchange(true, std::memory_order_acq_rel)) {
                delete counter_ptr;
            }
        }
    }
};

template<typename C>
struct Receiver {
    Counter<C>* counter_ptr { nullptr };

    auto counter() const -> Counter<C>& { return *counter_ptr; }

    auto acquire() const -> Receiver<C> {
        auto count = counter().receivers.fetch_add(1, std::memory_order_relaxed);
        return Receiver<C> { counter_ptr };
    }

    auto release(void (*disconnect)(C& q)) -> void {
        if (counter().receivers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            disconnect(*(counter().chan));

            if (counter().destroy.exchange(true, std::memory_order_acq_rel)) {
                delete counter_ptr;
            }
        }
    }
};

export template<typename C>
auto create(C&& chan) -> std::tuple<Sender<C>, Receiver<C>> {
    auto counter = new Counter(1, 1, false, std::forward<C>(chan));
    return { Sender { &counter }, Receiver { &counter } };
}

} // namespace mpmc::counter