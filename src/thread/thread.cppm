module;
#include "rstd/macro.hpp"
export module rstd.thread:thread;
export import :id;

export import rstd.alloc;
// export import :thread.main_thread;

using rstd::alloc::string::String;

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

    // ref<str> as_str() const noexcept {
    //     // SAFETY mirror: guaranteed UTF-8 by construction convention
    //     // return inner.to_string_view_bytes(); // expect: view excluding trailing '\0'
    // }
};

} // namespace sys::thread::thread_name_string

using rstd::alloc::string::String;
using sys::thread::thread_name_string::ThreadNameString;

template<>
struct Impl<convert::From<String>, ThreadNameString>
    : ImplInClass<convert::From<String>, ThreadNameString> {};

/*

// The internal representation of a `Thread` handle
//
// We explicitly set the alignment for our guarantee in Thread::into_raw.
// This allows applications to stuff extra metadata bits into the alignment.
struct alignas(8) Inner {
    std::optional<ThreadNameString> name;
    ThreadId id;
    sys::sync::Parker parker;

    Inner() = delete;

    Pin<const sys::sync::Parker&> parker_pin(Pin<const Inner&> self) const noexcept {
        // Mirror `Pin::map_unchecked`
        return Pin<const sys::sync::Parker&>::map_unchecked(self, [](const Inner& in) -> const
sys::sync::Parker& { return in.parker;
        });
    }
};

export class Thread {
public:
    Thread() = delete;
    Thread(const Thread&) = default;
    Thread& operator=(const Thread&) = default;

    // internal constructor used by the runtime/spawn path
    static Thread new_(ThreadId id, std::optional<std::string> name) {
        std::optional<ThreadNameString> tn;
        if (name) tn = ThreadNameString::from(std::move(*name));

        // We need in-place construction for Parker on some platforms.
        // SAFETY mirror: we pin the Arc immediately after init so address never changes.
        auto inner = [&]() -> Pin<sync::Arc<Inner, alloc::System>> {
            auto arc = sync::Arc<Inner, alloc::System>::new_uninit_in(alloc::System{});

            auto* ptr = sync::Arc<Inner, alloc::System>::get_mut_unchecked(arc).as_mut_ptr();

            // write fields
            ::new (static_cast<void*>(std::addressof(ptr->name)))
decltype(ptr->name)(std::move(tn));
            ::new (static_cast<void*>(std::addressof(ptr->id)))   ThreadId(id);

            // in-place parker
            sys::sync::Parker::new_in_place(std::addressof(ptr->parker));

            return Pin<sync::Arc<Inner, alloc::System>>::new_unchecked(arc.assume_init());
        }();

        return Thread(std::move(inner));
    }

    void unpark() const noexcept {
        inner_.as_ref().get().parker_pin(inner_.as_ref()).get().unpark();
    }

    ThreadId id() const noexcept {
        return inner_.get().id;
    }

    std::optional<std::string_view> name() const noexcept {
        if (inner_.get().name) return inner_.get().name->as_str();
        return std::nullopt;
    }

    // For platform thread naming APIs etc.
    std::optional<const ffi::CStr*> cname() const noexcept {
        if (inner_.get().name) {
            return std::addressof(inner_.get().name->as_cstr());
        }
        if (main_thread::get() == std::optional<ThreadId>(inner_.get().id)) {
            return std::addressof(ffi::CStr::from_literal("main")); // expect helper; otherwise
store a static
        }
        return std::nullopt;
    }

    // ---- unstable: thread_raw (mirror rust) ----
    // Opaque raw pointer; keeps Pin invariant by only exposing Inner address.
    const void* into_raw() && noexcept {
        auto arc = Pin<sync::Arc<Inner, alloc::System>>::into_inner_unchecked(std::move(inner_));
        return static_cast<const void*>(sync::Arc<Inner,
alloc::System>::into_raw_with_allocator(std::move(arc)).first);
    }

    static Thread from_raw(const void* p) noexcept {
        // SAFETY: caller upholds provenance + refcount rules, like Rust contract.
        auto arc = sync::Arc<Inner, alloc::System>::from_raw_in(static_cast<const Inner*>(p),
alloc::System{}); return Thread(Pin<sync::Arc<Inner,
alloc::System>>::new_unchecked(std::move(arc)));
    }

private:
    explicit Thread(Pin<sync::Arc<Inner, alloc::System>> inner) : inner_(std::move(inner)) {}

    Pin<sync::Arc<Inner, alloc::System>> inner_;
};

// Debug (optional; keep if your fmt infra matches)
export inline void debug_fmt(fmt::Formatter& f, const Thread& t) {
    f.debug_struct("Thread")
        .field("id", t.id())
        .field("name", t.name())
        .finish_non_exhaustive();
}
        */

} // namespace rstd
