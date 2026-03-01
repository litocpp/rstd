module;
#include <rstd/macro.hpp>
export module rstd:thread.lifecycle;
export import :thread.thread;
export import :thread.scoped;
export import :thread.current;
export import :thread.forward;
export import :sys;
export import rstd.core;

using rstd::sync::Arc;
using rstd::thread::scoped::ScopeData;
namespace imp = rstd::sys::thread;

namespace rstd::thread::lifecycle
{

template<typename T>
struct Packet {
    Option<rstd::sync::Arc<ScopeData>> scope;
    Option<Result<T>>                  result;
};

template<>
struct Packet<void> {
    Option<rstd::sync::Arc<ScopeData>> scope;
    Option<Result<empty>>              result;
};

template<typename T>
struct JoinInner {
    using ret_t = meta::void_empty_t<T>;

    Thread         thread_;
    Arc<Packet<T>> packet;
    imp::Thread    native;

    USE_TRAIT(JoinInner)

    auto is_finished(this Self const& self) -> bool { return self.packet->strong_count() == 1; }

    auto thread(this Self const& self) -> Thread const& { return self.thread_; }

    auto join(this Self&& self) -> Result<ret_t> {
        self.native.join();

        return self.packet.get_mut()
            .expect("threads should not terminate unexpectedly")
            ->result.take()
            .unwrap();
    }
};

template<typename F>
auto spawn_unchecked(Option<String> name, usize stack_size, Option<Arc<ScopeData>> scope_data,
                     F&& f) -> io::Result<JoinInner<meta::invoke_result_t<F>>>
// requires Impled<F, FnOnce<void()>> && meta::special_of<meta::remove_cvref_t<F>,
// rstd::alloc::boxed::Box>
{
    using namespace rstd::alloc::boxed;
    using ret_t = meta::invoke_result_t<F>;

    auto id     = ThreadId::make();
    auto thread = Thread::make(id, rstd::move(name));

    auto packet = Arc<Packet<ret_t>>::make_uninit();
    auto ptr    = (void*)packet->as_ptr();

    ::new (ptr) Packet<ret_t> { .scope = rstd::move(scope_data), .result = None() };

    auto my_packet    = packet.assume_init();
    auto their_packet = my_packet.clone();

    auto start = [f = rstd::move(f), p = my_packet.clone()] mutable {
        if constexpr (meta::same_as<ret_t, void>) {
            f();
            p->result = Some(Ok<empty, int>(empty {}));
        } else {
            p->result = Some(Ok<ret_t, int>(rstd::move(f)()));
        }
    };

    auto&& init = Box<ThreadInit>::make(ThreadInit {
        .handle = rstd::move(thread), .start = Box<dyn<FnMut<void()>>>::make(rstd::move(start)) });

    return imp::Thread::make(stack_size, rstd::move(init)).map([&](auto&& native) {
        return JoinInner<ret_t> {
            .thread_ = rstd::move(thread),
            .packet  = rstd::move(their_packet),
            .native  = rstd::move(native),
        };
    });
}

} // namespace rstd::thread::lifecycle

namespace rstd::thread
{
void ThreadInit::init(this ThreadInit const& self) {
    // if let Err(_thread) = set_current(self.handle.clone()) {
    //     // The current thread should not have set yet. Use an abort to save binary size (see
    //     #123356). rtabort!("current thread handle already set during thread spawn");
    // }

    if (auto name = self.handle.cname()) {
        imp::Thread::set_name(*name);
    }
}
} // namespace rstd::thread
