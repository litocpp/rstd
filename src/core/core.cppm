export module rstd.core:core;
export import :basic;
export import :meta;

namespace rstd
{
export template<typename T>
void swap(T& a, T& b) noexcept(meta::is_nothrow_copy_constructible_v<T>) {
    T t(a);
    a = b;
    b = t;
}

export struct Empty {};
export template<typename>
struct EmptyT {};

export template<typename T>
using param_t = meta::conditional_t<meta::is_trivially_copy_constructible_v<T>, T, T&&>;
export template<typename T>
using param_ref_t = meta::conditional_t<meta::is_trivially_copy_constructible_v<T>, T, T&>;

export template<typename T>
[[nodiscard, gnu::always_inline]] inline param_t<T> param_forward(param_ref_t<T> t) {
    if constexpr (meta::is_trivially_copy_constructible_v<T>) {
        return t;
    } else {
        return static_cast<T&&>(t);
    }
}

static_assert(meta::is_trivially_copy_constructible_v<i32>);
static_assert(meta::is_trivially_copy_constructible_v<i32&>);

export [[noreturn]] inline void unreachable() {
    // Uses compiler specific extensions if possible.
    // Even if no extension is used, undefined behavior is still raised by
    // an empty function body and the noreturn attribute.
#if defined(_MSC_VER) && ! defined(__clang__) // MSVC
    __assume(false);
#else // GCC, Clang
    __builtin_unreachable();
#endif
}

export template<typename Self, typename T>
struct ref_base {
    using value_type = T;

    constexpr T* operator->() const noexcept { return static_cast<Self const*>(this)->p; }
    constexpr T& operator*() const noexcept { return *(static_cast<Self const*>(this)->p); }

    constexpr rstd::strong_ordering operator<=>(const ref_base& o) const noexcept = default;

    constexpr bool operator==(const T& other) const noexcept {
        return *(static_cast<Self const*>(this)->p) == other;
    }

    static constexpr auto from_raw(T& p) noexcept -> Self { return { .p = rstd::addressof(p) }; }
};
template<typename Self, typename T>
struct ref_base<Self, T[]> : ref_base<Self, T> {
    using value_type = T;

    constexpr auto operator[](usize i) const noexcept {
        return *(static_cast<Self const*>(this)->p + i);
    }
    constexpr auto len() const noexcept { return static_cast<Self const*>(this)->length; }

    static constexpr auto from_raw(T& p, usize length) noexcept -> Self requires meta::is_aggregate_v<Self>  {
        return { .p = rstd::addressof(p), .length = length };
    }
};

export template<typename Self, typename T>
struct ptr_base {
    using value_type = T;

    static constexpr auto from_raw(T* p) noexcept -> Self { return { .p = p }; }

    constexpr T* operator->() const noexcept { return static_cast<Self const*>(this)->p; }
    constexpr T& operator*() const noexcept { return *(static_cast<Self const*>(this)->p); }

    constexpr rstd::strong_ordering operator<=>(const ptr_base& o) const noexcept = default;

    constexpr bool operator==(T* other) const noexcept {
        return static_cast<Self const*>(this)->p == other;
    }
    constexpr bool operator==(rstd::nullptr_t) const noexcept {
        return static_cast<Self const*>(this)->p == nullptr;
    }
};

template<typename Self, typename T>
struct ptr_base<Self, T[]> : ptr_base<Self, T> {
    using value_type = T;

    static constexpr auto from_raw(T* p, usize length) noexcept -> Self {
        return { .p = p, .length = length };
    }

    constexpr auto operator[](usize i) const noexcept {
        return *(static_cast<Self const*>(this)->p + i);
    }
    constexpr auto len() const noexcept { return static_cast<Self const*>(this)->length; }
};

export template<typename T>
struct ptr;

export template<typename T>
struct ref : ref_base<ref<T>, T> {
    static_assert(meta::destructible<T>);

    T* p { nullptr };

    using Self = ref;
    constexpr auto as_ptr() const noexcept -> ptr<T>;
};

export template<typename T>
struct ptr : ptr_base<ptr<T>, T> {
    static_assert(meta::destructible<T>);

    T* p { nullptr };

    using Self = ptr;

    constexpr      operator T*() const noexcept { return this->p; }
    constexpr auto as_ref() const noexcept { return ref<T> { .p = this->p }; }
};

template<typename T>
auto constexpr ref<T>::as_ptr() const noexcept -> ptr<T> {
    return ptr<T> { this->p };
}

template<typename T>
struct ref<T[]> : ref_base<ref<T[]>, T[]> {
    T*    p { nullptr };
    usize length;

    auto constexpr as_ptr() const noexcept -> ptr<T[]> {
        return ptr<T[]>::from_raw(this->p, length);
    }
};

template<typename T>
struct ptr<T[]> : ptr_base<ptr<T[]>, T[]> {
    T*    p { nullptr };
    usize length;

    auto constexpr as_ref() const noexcept -> ref<T[]> {
        return ref<T[]>::from_raw(this->p, length);
    }
};

export template<typename T>
using slice = ref<T[]>;

export template<typename U, typename T>
auto as_cast(ptr<T> p) noexcept -> ptr<U> {
    auto addr = p.p;
    if constexpr (meta::is_array_v<U>) {
        return ptr<U>::from_raw(static_cast<meta::add_pointer_t<meta::remove_extent_t<U>>>(addr),
                                p.len());
    } else {
        return ptr<U>::from_raw(static_cast<U*>(addr));
    }
}

export template<typename U, typename T>
auto as_cast(ref<T> p) noexcept -> ref<U> {
    auto addr = p.p;
    if constexpr (meta::is_array_v<U>) {
        return ref<U>::from_raw(static_cast<meta::add_pointer_t<meta::remove_extent_t<U>>>(addr),
                                p.len());
    } else {
        return ref<U>::from_raw(static_cast<meta::add_pointer_t<U>>(addr));
    }
}

export template<typename U>
[[gnu::always_inline]] inline auto as_cast(u8* s) noexcept -> U {
    return reinterpret_cast<U>(s);
}
export template<typename U>
[[gnu::always_inline]] inline auto as_cast(u8 const* s) noexcept -> U {
    return reinterpret_cast<U>(s);
}

} // namespace rstd
