module;

#include <atomic>
#include <optional>
#include <chrono>
#include <tuple>
#include <condition_variable>
#include <mutex>
#include <chrono>

export module mpsc;
export import mpmc;

// #include <tbb/concurrent_queue.h>

namespace mpsc
{

// template<typename T>
// auto channel() -> std::tuple<Sender<T>, Receiver<T>>;

export template<typename T>
struct Sender {
    // friend auto channel<T>() -> std::tuple<Sender<T>, Receiver<T>>;
    //    using Inner = details::channel::Sender<details::channel::TbbChannel<T>>;
    //    auto chan() const -> typename Inner::chan_t& { return *(inner.counter().chan); }

    mpmc::Sender<T> inner;

    Sender(): inner() {}
    Sender(const Sender& o): inner(o.inner.acquire()) {}
    ~Sender() {
        // inner.release([](auto& c) {
        //     c.disconnect();
        // });
    }

    Sender& operator=(const Sender& o) {
        inner = o.inner.acquire();
        return *this;
    }

    Sender(Sender&&)           = delete;
    Sender operator=(Sender&&) = delete;

    auto send(T) const -> bool;
    auto try_send(T) const -> bool;
};

} // namespace mpsc

// template<typename T>
// class Receiver {
//     friend auto channel<T>() -> std::tuple<Sender<T>, Receiver<T>>;
// 
// public:
//     Receiver(const Receiver& o): inner(o.inner.acquire()) {}
//     ~Receiver() {
//         inner.release([](auto& c) {
//             c.disconnect();
//         });
//     }
// 
//     Receiver& operator=(const Receiver& o) {
//         inner = o.inner.acquire();
//         return *this;
//     }
// 
//     Receiver(Receiver&&)           = delete;
//     Receiver operator=(Receiver&&) = delete;
// 
//     auto try_recv() -> std::optional<T>;
//     auto recv() -> std::optional<T>;
//     template<typename TR, typename TP>
//     auto recv_timeout(const std::chrono::duration<TR, TP>&) -> std::optional<T>;
//     // iter
//     // try_iter
// private:
//     using Inner = details::channel::Receiver<details::channel::TbbChannel<T>>;
//     Receiver(Inner inner): inner(inner) {}
//     auto chan() const -> typename Inner::chan_t& { return *(inner.counter().chan); }
// 
//     Inner inner;
// };
// 
// // template<typename T>
// // auto channel() -> std::tuple<Sender<T>, Receiver<T>> {
// //     using C     = details::channel::TbbChannel<T>;
// //     auto [s, r] = details::channel::create<C>(std::make_unique<C>());
// //     return { Sender<T>(s), Receiver<T>(r) };
// // }
// 
// template<typename T>
// auto Sender<T>::send(T t) const -> bool {
//     return this->chan().send(t);
// }
// template<typename T>
// auto Sender<T>::try_send(T t) const -> bool {
//     return this->chan().try_send(t);
// }
// 
// template<typename T>
// auto Receiver<T>::try_recv() -> std::optional<T> {
//     return this->chan().try_recv();
// }
// template<typename T>
// auto Receiver<T>::recv() -> std::optional<T> {
//     return this->chan().recv();
// }
// 
// template<typename T>
// template<typename TR, typename TP>
// auto Receiver<T>::recv_timeout(const std::chrono::duration<TR, TP>& dur) -> std::optional<T> {
//     return this->chan().recv(dur);
// }