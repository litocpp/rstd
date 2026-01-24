module;
#include "rstd/macro.hpp"
export module rstd.thread:thread;
export import :id;

export import rstd.alloc;
// export import :thread.main_thread;

using rstd::alloc::string::String;
using rstd::pin::Pin;
using rstd::sync::Arc;

namespace rstd
{

namespace sys::thread::thread_name_string
{

// Like a `String` it's guaranteed UTF-8 and like a `CString` it's null terminated.
// (C++ side we assume std::string is UTF-8 by convention, and CString validates no interior '\0')
class ThreadNameString {
    ffi::CString inner;

    explicit ThreadNameString(alloc::ffi::CString c): inner(rstd::move(c)) {}

public:
    USE_TRAIT(ThreadNameString)

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

namespace thread
{
struct Inner {};

export class Thread {
    Pin<Arc<Inner>> inner;

    Thread(Pin<Arc<Inner>> inner): inner(rstd::move(inner)) {}

public:
    USE_TRAIT(Thread)

    static auto make(ThreadId id, Option<String> name) -> Thread {
        // let name = name.map(ThreadNameString::from);

        // // We have to use `unsafe` here to construct the `Parker` in-place,
        // // which is required for the UNIX implementation.
        // //
        // // SAFETY: We pin the Arc immediately after creation, so its address never
        // // changes.
        // let inner = unsafe {
        //     let mut arc = Arc::<Inner, _>::new_uninit_in(System);
        //     let ptr = Arc::get_mut_unchecked(&mut arc).as_mut_ptr();
        //     (&raw mut (*ptr).name).write(name);
        //     (&raw mut (*ptr).id).write(id);
        //     Parker::new_in_place(&raw mut (*ptr).parker);
        //     Pin::new_unchecked(arc.assume_init())
        // };
        // Thread { inner }
        auto inner = Pin<Arc<Inner>>::make_unchecked(Arc<Inner>::make({}));
        return { rstd::move(inner) };
    }
};

} // namespace thread
} // namespace rstd
