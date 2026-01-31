module;
#include <rstd/macro.hpp>
export module rstd.thread:builder;
export import rstd.alloc;

using rstd::alloc::string::String;

namespace rstd::thread::builder
{

/// Thread factory, which can be used in order to configure the properties of
/// a new thread.
struct Builder {
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

    static auto make() -> Self {
        return { {
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


    // template<typename F, typename T>
    // auto spawn_unchecked(this Self self, F&& f) -> io::Result<JoinHandle<T>>
    // where
    //     F: FnOnce() -> T,
    //     F: Send,
    //     T: Send,
    // {
    //     let Builder { name, stack_size, no_hooks } = self;
    //     Ok(JoinHandle(unsafe { spawn_unchecked(name, stack_size, no_hooks, None, f) }?))
    // }
};

} // namespace rstd::thread::builder