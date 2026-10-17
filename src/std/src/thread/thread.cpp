module rstd.thread.thread;
import rstd.sys.thread;

using namespace rstd::literals;

namespace rstd::thread
{
auto Thread::clone() const -> Thread {
    return Thread { as<clone::Clone>(inner).clone() };
}

auto Thread::make(ThreadId id, Option<String> name) -> Thread {
    // Convert String to ThreadNameString if present
    auto thread_name = name.map([](String s) {
        return ThreadNameString::from(rstd::move(s));
    });

    auto arc = Arc<Inner>::make_uninit();
    auto ptr = (void*)arc->as_ptr();

    ::new (ptr) Inner { .name { rstd::move(thread_name) }, .id { id }, .parker {} };

    return Thread { arc.assume_init() };
}

auto Thread::id() const noexcept -> ThreadId {
    return inner->id;
}

auto Thread::name() const -> Option<ThreadNameString> {
    if (! inner) {
        rstd::panic { "Thread::name() called with null Arc" };
    }
    if (auto& name = inner->name; name) {
        return name.clone();
    } else if (main_thread::get() == Some(id())) {
        return Some(ThreadNameString { ::alloc::ffi::CString::from_raw_parts("main") });
    } else {
        return None();
    }
}

void Thread::unpark() const noexcept {
    inner->parker.unpark();
}

void Thread::park() const {
    inner->parker.park();
}

void Thread::park_timeout(rstd::time::Duration timeout) const {
    inner->parker.park_timeout(timeout);
}

auto Thread::cname() const noexcept -> Option<ref<ffi::CStr>> {
    if (inner->name.is_some()) {
        return Some(inner->name.as_ref().unwrap().as_cstr());
    }
    if (main_thread::get() == Some(id())) {
        return Some(ffi::CStr::from_ptr("main"));
    }
    return None();
}

auto Thread::into_raw() -> voidp {
    return inner.into_raw().into_raw();
}

auto Thread::from_raw(voidp p) -> Thread {
    return Thread { Arc<Inner>::from_raw(ArcRaw<Inner>::from_raw(p)) };
}
} // namespace rstd::thread

namespace rstd::thread
{
void ThreadInit::init() const {
    if (set_current(as<clone::Clone>(handle).clone()).is_err()) {
        panic { "current thread handle already set during thread spawn" };
    }

    if (auto name = handle.cname()) {
        sys::thread::Thread::set_name(*name);
    }
}
} // namespace rstd::thread
