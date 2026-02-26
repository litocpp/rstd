module;
#include <rstd/macro.hpp>
export module rstd:thread.builder;
export import :alloc;
export import :thread.join_handle;
export import :thread.lifecycle;
export import :sys;
export import rstd.core;

using rstd::alloc::string::String;
namespace imp = rstd::sys::thread;

namespace rstd::thread::builder
{

/// Thread factory, which can be used in order to configure the properties of
/// a new thread.
export struct Builder {
    struct Fields {
        /// A name for the thread-to-be, for identification in panic messages
        Option<String> name;
        /// The size of the stack for the spawned thread in bytes
        Option<usize> stack_size;
        /// Skip running and inheriting the thread spawn hooks
        bool no_hooks;
    };

    Fields d;

    USE_TRAIT(Builder)

    static auto make() -> Builder {
        return { .d = {
                     .name {},
                     .stack_size {},
                     .no_hooks = false,
                 } };
    }

    auto name(this Self self, String name) -> Builder {
        self.d.name = Some(rstd::move(name));
        return self;
    }

    auto stack_size(this Self self, usize size) -> Builder {
        self.d.stack_size = Some(size);
        return self;
    }

    auto no_hooks(this Self self) -> Builder {
        self.d.no_hooks = true;
        return self;
    }

    template<typename F>
    auto spawn(F&& f) -> io::Result<JoinHandle<meta::invoke_result_t<F>>>
    // requires Impled<meta::remove_cvref_t<F>, FnOnce<void()>> &&
    // meta::same_as<meta::remove_cvref_t<F>, Box<dyn<FnOnce<void()>>>
    {
        auto stack_size = d.stack_size.unwrap_or(0);

        auto inner =
            lifecycle::spawn_unchecked(rstd::move(d.name), stack_size, None(), rstd::forward<F>(f));

        if (inner.is_ok()) {
            return Ok(JoinHandle<meta::invoke_result_t<F>>::make(inner.unwrap_unchecked()));
        } else {
            return Err(inner.unwrap_err_unchecked());
        }
    }
};

} // namespace rstd::thread::builder