module;
#include <rstd/macro.hpp>
export module rstd:thread.lifecycle;
export import :thread.thread;
export import :thread.scoped;
export import :thread.forward;
export import :sys;

using rstd::thread::scoped::ScopeData;
namespace imp = rstd::sys::thread;

namespace rstd::thread::lifecycle
{

template<typename T>
struct Packet {
    Option<Arc<ScopeData>> scope;
    Option<Result<T>>      result;
};

template<typename T>
struct JoinInner {
    Thread         thread_;
    Arc<Packet<T>> packet;
    imp::Thread    native;

    USE_TRAIT(JoinInner)

    auto is_finished(this Self const& self) -> bool { return self.packet.strong_count() == 1; }

    auto thread(this Self const& self) -> Thread& { return self.thread; }

    auto join(this Self self) -> Result<T> {
        return {};
        // self.native.join();
        // Arc::get_mut(&mut self.packet)
        //     // FIXME(fuzzypixelz): returning an error instead of panicking here
        //     // would require updating the documentation of
        //     // `std::thread::Result`; currently we can return `Err` if and only
        //     // if the thread had panicked.
        //     .expect("threads should not terminate unexpectedly")
        //     .result
        //     .get_mut()
        //     .take()
        //     .unwrap()
    }
};

} // namespace rstd::thread::lifecycle