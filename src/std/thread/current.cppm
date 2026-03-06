export module rstd:thread.current;
export import :thread.thread;

namespace rstd::thread
{

inline thread_local voidp CURRENT { nullptr };

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

void drop_current();

struct CurrentGuard {
    ~CurrentGuard() { drop_current(); }
};

export auto try_current() -> Option<Thread> {
    if (CURRENT == nullptr || CURRENT == BUSY || CURRENT == DESTROYED) {
        return None();
    }
    auto thread = Thread::from_raw(CURRENT);
    auto res    = thread.clone();
    thread.into_raw(); // Don't drop it.
    return rstd::option::Some<Thread>(rstd::move(res));
}

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

    thread_local CurrentGuard GUARD;
    (void)GUARD;

    CURRENT = thread.into_raw();
    return Ok(empty {});
}

export auto current() -> Thread {
    if (auto t = try_current(); t.is_some()) {
        return t.unwrap_unchecked();
    }

    // Lazy initialization for the current thread (e.g. main thread or thread not spawned by rstd)
    auto id = ThreadId::make();
    if (main_thread::get().is_none()) {
        main_thread::set(id);
    }
    auto thread = Thread::make(id, None());
    // Use the member function syntax as requested
    thread.clone().set_current().unwrap_unchecked();
    return thread;
}

void drop_current() {
    auto current = CURRENT;
    if (current > DESTROYED) {
        CURRENT = DESTROYED;
        Thread::from_raw(current);
    }
}

} // namespace rstd::thread
