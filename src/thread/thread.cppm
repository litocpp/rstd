module;
#include <rstd/macro.hpp>
export module rstd:thread.thread;
export import :thread.id;
export import :thread.main_thread;

export import :alloc;
export import :sys.sync.thread_parking;

using rstd::alloc::boxed::Box;
using rstd::alloc::string::String;
using rstd::pin::Pin;
using rstd::sync::Arc;
using rstd::sys::sync::thread_parking::Parker;

namespace rstd
{

namespace sys::thread::thread_name_string
{

// Like a `String` it's guaranteed UTF-8 and like a `CString` it's null terminated.
// (C++ side we assume std::string is UTF-8 by convention, and CString validates no interior '\0')
class ThreadNameString {
    ffi::CString inner;

public:
    USE_TRAIT(ThreadNameString)

    explicit ThreadNameString(alloc::ffi::CString c): inner(rstd::move(c)) {}

    ThreadNameString(Self&&) noexcept            = default;
    ThreadNameString& operator=(Self&&) noexcept = default;

    static auto from(String s) -> Self {
        return Self { ffi::CString::make(rstd::move(s))
                          .expect("thread name may not contain interior null bytes") };
    }

    auto as_cstr() const noexcept { return inner.as_ref(); }

    auto as_str() const noexcept -> ref<str> { return inner.to_bytes(); }
};

} // namespace sys::thread::thread_name_string

using rstd::alloc::string::String;
using sys::thread::thread_name_string::ThreadNameString;

template<>
struct Impl<convert::From<String>, ThreadNameString>
    : ImplInClass<convert::From<String>, ThreadNameString> {};

template<>
struct Impl<clone::Clone, ThreadNameString> : ImplDefault<clone::Clone, ThreadNameString> {
    auto clone() -> ThreadNameString {
        return ThreadNameString { as<clone::Clone>(this->self().inner).clone() };
    }
};

namespace thread
{

/// The internal representation of a `Thread` handle
/// We explicitly set the alignment for our guarantee in Thread::into_raw.
struct alignas(8) Inner {
    Option<ThreadNameString> name;
    ThreadId                 id;
    Parker                   parker;
};

export class Thread : public WithTrait<Thread, clone::Clone> {
    Pin<Arc<Inner>> inner;

    explicit Thread(Pin<Arc<Inner>> inner): inner(rstd::move(inner)) {}

public:
    USE_TRAIT(Thread)

    /// Creates a new thread handle.
    static auto make(ThreadId id, Option<String> name) -> Thread {
        // Convert String to ThreadNameString if present
        auto thread_name = name.map([](String s) {
            return ThreadNameString::from(rstd::move(s));
        });

        auto arc = Arc<Inner>::make_uninit();
        auto ptr = (void*)arc->as_ptr();

        ::new (ptr) Inner { .name { rstd::move(thread_name) }, .id { id }, .parker {} };

        auto pinned = Pin<Arc<Inner>>::make_unchecked(arc.assume_init());

        return Thread { rstd::move(pinned) };
    }

    /// Gets the thread's unique identifier.
    auto id() const noexcept -> ThreadId { return inner.get_ref()->id; }

    /// Gets the thread's name as a string reference if available.
    /// Returns None if no name was set.
    auto name() const -> Option<ThreadNameString> {
        if (auto& name = inner->as_ptr()->name; name) {
            return name.clone();
        } else if (main_thread::get() == Some(id())) {
            return Some(ThreadNameString { ffi::CString::from_raw_parts("main") });
        } else {
            return None();
        }
    }

    /// Atomically makes the handle's token available if it is not already.
    ///
    /// Every thread is equipped with some basic low-level blocking support, via
    /// the park() function and the unpark() method. These can be used as a more
    /// CPU-efficient implementation of a spinlock.
    void unpark() const noexcept { inner->as_ptr()->parker.unpark(); }

    /// Like the public park, but callable on any handle.
    ///
    /// # Safety
    /// May only be called from the thread to which this handle belongs.
    void park() const { inner->as_ptr()->parker.park(); }

    /// Like the public park_timeout, but callable on any handle.
    ///
    /// # Safety
    /// May only be called from the thread to which this handle belongs.
    void park_timeout(cppstd::chrono::duration<double> timeout) const {
        inner->as_ptr()->parker.park_timeout(timeout);
    }

    /// Gets the C string representation of the thread name, if available.
    auto cname() const noexcept -> Option<ref<ffi::CStr>> {
        if (inner.get_ref()->name.is_some()) {
            return Some(inner.get_ref()->name.as_ref().unwrap().as_cstr());
        }
        if (main_thread::get() == Some(id())) {
            return Some(ffi::CStr::from_ptr("main"));
        }
        return None();
    }
};

export struct ThreadInit {
    Thread                   handle;
    Box<dyn<FnOnce<void()>>> start;
};

} // namespace thread
} // namespace rstd
