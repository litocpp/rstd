module;
#include <rstd/macro.hpp>

export module rstd.alloc:vec;
export import :boxed;
export import :alloc;
export import rstd.core;

using ::alloc::Allocator;
using ::alloc::boxed::Box;

using rstd::alloc::Layout;
using rstd::ptr_::non_null::NonNull;
using rstd::ptr_::unique::Unique;

using namespace rstd::prelude;

namespace alloc::vec
{

/// A low-level utility for managing the backing storage of a `Vec`.
/// It handles allocation and deallocation of raw memory via global allocator.
template<typename T>
struct RawVec {
    Unique<T> ptr;
    usize     cap;

    constexpr RawVec(): ptr(Unique<T>::dangling()), cap(0) {}
    constexpr RawVec(Unique<T> p, usize c): ptr(rstd::move(p)), cap(c) {}

    static auto with_capacity(usize capacity) -> RawVec {
        if (capacity == 0) return RawVec();
        auto layout = Layout::array<T>(capacity).unwrap();
        auto res    = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto p = res.unwrap_unchecked().as_mut_ptr().template cast<T>();
        return RawVec(Unique<T>::make_unchecked(p), capacity);
    }

    /// Reallocates the storage to a new capacity.
    void grow(usize new_cap) {
        if (new_cap <= cap) return;

        auto new_layout = Layout::array<T>(new_cap).unwrap();

        if (cap == 0) {
            auto res = as<Allocator>(GLOBAL).allocate(new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);
            ptr = Unique<T>::make_unchecked(res.unwrap_unchecked().as_mut_ptr().template cast<T>());
        } else {
            auto old_layout = Layout::array<T>(cap).unwrap();
            auto old_ptr    = ptr.as_mut_ptr();

            auto res = as<Allocator>(GLOBAL).grow(
                NonNull<u8>::make_unchecked(old_ptr.template cast<u8>()), old_layout, new_layout);
            if (res.is_err()) handle_alloc_error(new_layout);

            ptr = Unique<T>::make_unchecked(res.unwrap_unchecked().as_mut_ptr().template cast<T>());
        }
        cap = new_cap;
    }

    ~RawVec() {
        if (cap > 0) {
            auto layout = Layout::array<T>(cap).unwrap();
            as<Allocator>(GLOBAL).deallocate(
                NonNull<u8>::make_unchecked(ptr.as_mut_ptr().template cast<u8>()), layout);
        }
    }

    // Disable copy
    RawVec(const RawVec&)            = delete;
    RawVec& operator=(const RawVec&) = delete;

    // Move
    constexpr RawVec(RawVec&& o) noexcept: ptr(rstd::move(o.ptr)), cap(o.cap) {
        o.cap = 0;
        o.ptr = Unique<T>::dangling();
    }
    constexpr RawVec& operator=(RawVec&& o) noexcept {
        if (this != &o) {
            this->~RawVec();
            ptr   = rstd::move(o.ptr);
            cap   = o.cap;
            o.cap = 0;
            o.ptr = Unique<T>::dangling();
        }
        return *this;
    }
};

export template<typename T>
class Vec {
    RawVec<T> buf;
    usize     m_len;

    static_assert(Impled<T, Sized>);

public:
    USE_TRAIT(Vec)

    constexpr Vec(): buf(), m_len(0) {}

    constexpr Vec(const Self&)            = delete;
    constexpr Vec& operator=(const Self&) = delete;

    constexpr Vec(Self&& o) noexcept: buf(rstd::move(o.buf)), m_len(o.m_len) { o.m_len = 0; }
    constexpr Vec& operator=(Self&& o) noexcept {
        if (this != &o) {
            clear();
            buf     = rstd::move(o.buf);
            m_len   = o.m_len;
            o.m_len = 0;
        }
        return *this;
    }

    ~Vec() { clear(); }

    static constexpr auto make() -> Self { return {}; }
    static auto           with_capacity(usize capacity) -> Self {
        Vec v;
        v.buf   = RawVec<T>::with_capacity(capacity);
        v.m_len = 0;
        return v;
    }

    constexpr auto as_slice() const noexcept -> slice<T> {
        if (m_len == 0) return {};
        return slice<T>::from_raw_parts(buf.ptr.as_ptr().as_raw_ptr(), m_len);
    }

    constexpr auto as_mut_slice() noexcept -> mut_ptr<T[]> {
        if (m_len == 0) return {};
        return mut_ptr<T[]>::from_raw_parts(buf.ptr.as_mut_ptr().as_raw_ptr(), m_len);
    }

    constexpr auto as_ptr() const noexcept -> ptr<T> { return buf.ptr.as_ptr(); }

    auto into_boxed_slice() noexcept -> Box<T[]> {
        auto length = m_len;
        auto layout = Layout::array<T>(length).unwrap();
        auto res    = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto* raw     = reinterpret_cast<T*>(res.unwrap_unchecked().as_mut_ptr().as_raw_ptr());
        auto* old_ptr = buf.ptr.as_mut_ptr().as_raw_ptr();
        for (usize i = 0; i < length; ++i) {
            new (raw + i) T(rstd::move(old_ptr[i]));
            old_ptr[i].~T();
        }
        auto b = Box<T[]>::from_raw(mut_ptr<T[]>::from_raw_parts(raw, length));
        m_len  = 0;
        return b;
    }

    constexpr void push(T&& value) {
        if (m_len == buf.cap) {
            buf.grow(buf.cap == 0 ? 4 : buf.cap * 2);
        }
        new (buf.ptr.as_mut_ptr().as_raw_ptr() + m_len) T(rstd::move(value));
        m_len++;
    }

    constexpr auto pop() -> Option<T> {
        if (m_len == 0) {
            return None();
        } else {
            m_len--;
            T* p     = buf.ptr.as_mut_ptr().as_raw_ptr() + m_len;
            T  value = rstd::move(*p);
            p->~T();
            return Some(value);
        }
    }

    constexpr void push_back(const T& value)
        requires Impled<T, Clone>
    {
        if (m_len == buf.cap) {
            buf.grow(buf.cap == 0 ? 4 : buf.cap * 2);
        }
        new (buf.ptr.as_mut_ptr().as_raw_ptr() + m_len) T(as<Clone>(value).clone());
        m_len++;
    }

    constexpr void pop_back() { (void)pop(); }

    constexpr T& at(usize index) {
        if (index >= m_len) rstd::panic("Vec index out of bounds");
        return buf.ptr.as_mut_ptr().as_raw_ptr()[index];
    }
    constexpr const T& at(usize index) const {
        if (index >= m_len) rstd::panic("Vec index out of bounds");
        return buf.ptr.as_ptr().as_raw_ptr()[index];
    }

    constexpr T&       operator[](usize index) { return at(index); }
    constexpr const T& operator[](usize index) const { return at(index); }

    constexpr usize len() const { return m_len; }
    constexpr usize capacity() const { return buf.cap; }
    constexpr bool  is_empty() const { return m_len == 0; }

    constexpr void clear() {
        auto* p = buf.ptr.as_mut_ptr().as_raw_ptr();
        for (usize i = 0; i < m_len; ++i) {
            p[i].~T();
        }
        m_len = 0;
    }

    constexpr T remove(usize index) {
        if (index >= m_len) rstd::panic("Vec index out of bounds");
        T     value = rstd::move(at(index));
        auto* p     = buf.ptr.as_mut_ptr().as_raw_ptr();
        for (usize i = index; i < m_len - 1; ++i) {
            p[i] = rstd::move(p[i + 1]);
        }
        p[m_len - 1].~T();
        m_len--;
        return value;
    }

    constexpr auto begin() noexcept { return buf.ptr.as_mut_ptr().as_raw_ptr(); }
    constexpr auto end() noexcept { return buf.ptr.as_mut_ptr().as_raw_ptr() + m_len; }
    constexpr auto begin() const noexcept { return buf.ptr.as_ptr().as_raw_ptr(); }
    constexpr auto end() const noexcept { return buf.ptr.as_ptr().as_raw_ptr() + m_len; }
};

} // namespace alloc::vec

export namespace rstd
{
using ::alloc::vec::Vec;
}

namespace rstd
{
template<typename U, mtp::same_as<cmp::PartialEq<::alloc::vec::Vec<U>>> T>
struct Impl<T, ::alloc::vec::Vec<U>> : ImplBase<default_tag<::alloc::vec::Vec<U>>> {
    auto eq(const ::alloc::vec::Vec<U>& other) const noexcept -> bool {
        if (this->self().len() != other.len()) return false;
        for (usize i = 0; i < this->self().len(); ++i) {
            if (! (this->self()[i] == other[i])) return false;
        }
        return true;
    }
};

template<typename A, mtp::same_as<From<::alloc::boxed::Box<A[]>>> T>
struct Impl<T, ::alloc::vec::Vec<A>> : ImplBase<::alloc::vec::Vec<A>> {
    static auto from(::alloc::boxed::Box<A[]> b) -> ::alloc::vec::Vec<A> {
        auto ptr = b.as_mut_ptr();
        auto len = ptr.len();
        auto vec = ::alloc::vec::Vec<A>::with_capacity(len);
        for (usize i = 0; i != len; ++i) {
            vec.push(rstd::move(ptr[i]));
        }
        return vec;
    }
};

} // namespace rstd
