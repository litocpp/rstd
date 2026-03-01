export module rstd:thread.current;
export import :thread.thread;

namespace rstd::thread
{

thread_local voidp CURRENT { nullptr };

constexpr voidp    NONE { ptr_::null_mut<void>() };
static const voidp BUSY { ptr_::without_provenance_mut<void>(1) };
static const voidp DESTROYED { ptr_::without_provenance_mut<void>(2) };

namespace id
{

thread_local Option<ThreadId> ID    = None();
constexpr auto                CHEAP = true;

auto get() -> Option<ThreadId> { return ID; }

void set(ThreadId id) { ID = (Some(id)); }
} // namespace id

export auto set_current(Thread thread) -> Result<empty, Thread> {
    if (CURRENT) {
        return Err(rstd::move(thread));
    }

    if (auto id_ = id::get()) {
        if (*id_ != thread.id()) {
            return Err(rstd::move(thread));
        }
    } else {
        id::set(thread.id());
    }

    //// Make sure that `crate::rt::thread_cleanup` will be run, which will
    //// call `drop_current`.
    // crate::sys::thread_local ::guard::enable();
    CURRENT = thread.into_raw();
    return Ok(empty {});
}

void drop_current() {
    auto current = CURRENT;
    if (current > DESTROYED) {
        CURRENT = DESTROYED;
        Thread::from_raw(current);
    }
}

} // namespace rstd::thread