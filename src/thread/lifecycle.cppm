module;
#include <rstd/macro.hpp>
export module rstd:thread.lifecycle;
export import :thread.thread;
export import :thread.scoped;
export import :thread.forward;
export import :sys;
export import rstd.core;

using rstd::thread::scoped::ScopeData;
namespace imp = rstd::sys::thread;

namespace rstd::thread::lifecycle
{

template<typename T>
struct Packet {
    Option<rstd::sync::Arc<ScopeData>> scope;
    // Option<result::Result<T>>                      result;
};

struct ThreadInit {
    Thread handle;
    // rstd::alloc::boxed::Box<dyn<FnOnce<void()>>> start;
};

extern "C" void* thread_start_rust(void* data);

template<typename T>
struct JoinInner {
    Thread                     thread_;
    rstd::sync::Arc<Packet<T>> packet;
    imp::Thread                native;

    USE_TRAIT(JoinInner)

    // JoinInner(Thread thread, rstd::sync::Arc<Packet<T>> packet, imp::Thread native)
    //     : thread_(rstd::move(thread)), packet(rstd::move(packet)), native(rstd::move(native)) {}

    auto is_finished(this Self const& self) -> bool { return self.packet->strong_count() == 1; }

    auto thread(this Self const& self) -> Thread const& { return self.thread_; }

    auto join(this Self self) -> Result<T> {
        // self.native.join().expect("thread::join failed");

        // if (rstd::sync::Arc::get_mut(&mut self.packet).is_none()) {
        //     panic("threads should not terminate unexpectedly");
        // }

        // return rstd::sync::Arc::get_mut(&mut self.packet)
        //     .expect("threads should not terminate unexpectedly")
        //     ->result.take()
        //     .expect("thread result not available");
    }
};

template<typename F, typename T>
auto spawn_unchecked(Option<String> name, usize stack_size,
                     Option<rstd::sync::Arc<ScopeData>> scope_data, F&& f)
    -> io::Result<JoinInner<T>>
    requires Impled<F, FnOnce<void()>> &&
             meta::special_of<meta::remove_cvref_t<F>, rstd::alloc::boxed::Box>
{
    using namespace rstd::alloc::boxed;
    using namespace rstd::sync;

    auto id     = ThreadId::make();
    auto thread = Thread::make(id, rstd::move(name));

    auto packet = Arc<Packet<T>>::make_uninit();
    auto ptr    = (void*)packet->as_ptr();

    ::new (ptr) Packet<T> { .scope = rstd::move(scope_data), .result = None() };

    auto my_packet    = packet.assume_init();
    auto their_packet = my_packet.clone();

    // TODO: Create Box<dyn<FnOnce<void()>>> properly
    // For now, return a simple JoinInner without proper execution

    // auto init = Box<ThreadInit>::make(ThreadInit { thread.clone(), Box<dyn<FnOnce<void()>>> {} });

    // auto native_result = imp::Thread::make(stack_size, rstd::move(init));
    // if (native_result.is_err()) {
    //     return Err(native_result.unwrap_err());
    // }

    // return Ok(JoinInner<T>(rstd::move(thread), rstd::move(my_packet), native_result.unwrap()));
}

} // namespace rstd::thread::lifecycle
