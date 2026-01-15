export module rstd.core:core;
export import :meta;

namespace rstd
{

export using cppstd::strong_ordering;
export using cppstd::nullptr_t;
export using cppstd::error_code;
export using cppstd::memory_order;
export using cppstd::numeric_limits;
export using cppstd::allocator;
export using cppstd::allocator_traits;
export using cppstd::source_location;
export using cppstd::align_val_t;

export using cppstd::invoke;
export using cppstd::max;
export using cppstd::min;
export using cppstd::get;
export using cppstd::get_if;
export using cppstd::addressof;
export using cppstd::construct_at;
export using cppstd::copy;
export using cppstd::copy_n;
export using cppstd::exchange;
export using cppstd::destroy_at;
export using cppstd::forward;
export using cppstd::move;
export using cppstd::memcpy;
export using cppstd::memcmp;
export using cppstd::memset;
export using cppstd::strlen;
export using cppstd::strncmp;
export using cppstd::char_traits;
export using cppstd::make_unsigned_t;
export using cppstd::bit_cast;
export using cppstd::default_delete;

export using meta::declval;

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
[[nodiscard, gnu::always_inline]]
param_t<T> param_forward(param_ref_t<T> t) {
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

export template<typename T>
struct ref {
    T& p;

    constexpr ref() noexcept = delete;
    constexpr ref(T& p) noexcept: p(p) {}
    constexpr ref(const ref& o) noexcept: p(o.p) {}
    constexpr ref(ref&& o) noexcept: p(o.p) {}

    constexpr T*   operator->() const noexcept { return &p; }
    constexpr T&   operator*() const noexcept { return p; }
    constexpr bool operator==(param_ref_t<T> in) const noexcept { return in == p; }
};
export template<typename T>
struct ptr {
    T* p { nullptr };

    using value_type = T;

    constexpr ptr() noexcept = default;
    constexpr ptr(T* p) noexcept: p(p) {}
    constexpr ptr(nullptr_t) noexcept {}

    constexpr T*   operator->() const noexcept { return p; }
    constexpr T&   operator*() const noexcept { return *p; }
    constexpr bool operator==(T* in) const noexcept { return in == p; }
    constexpr      operator T*() const noexcept { return p; }
    constexpr auto data() const noexcept { return p; }
    constexpr auto to_ref() const noexcept { return ref<T> { *p }; }
};

export template<typename T>
struct ref<T[]> : ref<T> {
    using ref<T>::ref;

    usize length { 0 };
};

export template<typename T>
struct ptr<T[]> : ptr<T> {
    using ptr<T>::ptr;

    usize length { 0 };

    constexpr auto size() const noexcept { return length; }
};

} // namespace rstd
