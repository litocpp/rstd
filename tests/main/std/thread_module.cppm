export module rstd.tests.std_thread_module;
import rstd.thread.thread;
import rstd.alloc;

using namespace rstd;
using namespace rstd::literals;

export auto std_thread_module() -> bool {
    auto id     = thread::ThreadId::make();
    auto handle = thread::Thread::make(id, Some(::alloc::string::String::make("module"_str)));
    auto copied = handle.clone();
    auto name   = copied.name();
    if (name.is_none() || name->as_str() != "module"_str) return false;
    auto restored = thread::Thread::from_raw(copied.into_raw());
    if (restored.id() != id) return false;
    handle.unpark();
    restored.park_timeout(time::Duration::from_millis(u64()));
    auto current = thread::current();
    return current.id() == thread::current_id() && thread::try_current()->id() == current.id();
}

export auto std_thread_module_current_id() -> uint64_t {
    return thread::current_id().as_u64().get().to_primitive();
}
