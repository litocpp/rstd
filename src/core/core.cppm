export module rstd.core:core;
export import :ptr.metadata;

namespace rstd
{

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

template<typename T, typename U>
auto from_raw_parts(U* self) noexcept -> T {
    if constexpr (requires { T::from_raw_parts(self->p, self->length); }) {
        return T::from_raw_parts(self->p, self->length);
    } else {
        return T::from_raw_parts(self->p);
    }
}

template<typename T, typename U, typename P>
auto from_raw_parts_override(U* self, P ptr) noexcept -> T {
    if constexpr (requires { T::from_raw_parts(ptr, self->length); }) {
        return T::from_raw_parts(ptr, self->length);
    } else {
        return T::from_raw_parts(ptr);
    }
}

export template<typename T>
struct ptr;
export template<typename T>
struct mut_ptr;
export template<typename T>
struct ref;
export template<typename T>
struct mut_ref;

export template<typename Self, typename T, bool Mutable>
struct ref_base {
    /// we only process T[] for vlaue_type
    using value_type = meta::conditional_t<Mutable, meta::remove_extent_t<T>,
                                           meta::add_const_t<meta::remove_extent_t<T>>>;

    constexpr value_type* operator->() const noexcept { return static_cast<Self const*>(this)->p; }
    constexpr value_type& operator*() const noexcept {
        return *(static_cast<Self const*>(this)->p);
    }

    constexpr rstd::strong_ordering operator<=>(const ref_base& o) const noexcept = default;

    constexpr bool operator==(const value_type& other) const noexcept {
        return *(static_cast<Self const*>(this)->p) == other;
    }

    constexpr auto as_ref() const noexcept -> ref<T>
        requires Mutable
    {
        return rstd::from_raw_parts<ref<T>>(static_cast<Self const*>(this));
    }

    constexpr auto as_ptr() const noexcept -> ptr<T> {
        return rstd::from_raw_parts<ptr<T>>(static_cast<Self const*>(this));
    }
    constexpr auto as_mut_ptr() const noexcept -> mut_ptr<T>
        requires Mutable
    {
        return rstd::from_raw_parts<mut_ptr<T>>(static_cast<Self const*>(this));
    }

    /// \name Normal
    /// @{
    static constexpr auto from_raw_parts(value_type* p) noexcept -> Self
        requires(! meta::DST<T>)
    {
        return { .p = p };
    }
    /// @}

    /// \name Requires: DSTArray
    /// @{
    constexpr auto operator[](usize i) const noexcept
        requires meta::DSTArray<T>
    {
        return *(static_cast<Self const*>(this)->p + i);
    }

    constexpr auto len() const noexcept
        requires meta::DSTArray<T>
    {
        return static_cast<Self const*>(this)->length;
    }

    static constexpr auto from_raw_parts(value_type* p, usize length) noexcept -> Self
        requires meta::DSTArray<T> && meta::is_aggregate_v<Self>
    {
        return { .p = p, .length = length };
    }
    /// @}
};

export template<typename Self, typename T, bool Mutable>
struct ptr_base {
    /// we only process T[] for vlaue_type
    using value_type = meta::conditional_t<Mutable, meta::remove_extent_t<T>,
                                           meta::add_const_t<meta::remove_extent_t<T>>>;

    constexpr value_type* operator->() const noexcept { return static_cast<Self const*>(this)->p; }
    constexpr value_type& operator*() const noexcept {
        return *(static_cast<Self const*>(this)->p);
    }

    constexpr rstd::strong_ordering operator<=>(const ptr_base& o) const noexcept = default;

    constexpr bool operator==(const value_type& other) const noexcept {
        return *(static_cast<Self const*>(this)->p) == other;
    }
    constexpr bool operator==(rstd::nullptr_t) const noexcept {
        return static_cast<Self const*>(this)->p == nullptr;
    }

    constexpr auto as_ptr() const noexcept -> ptr<T>
        requires Mutable
    {
        return rstd::from_raw_parts<ptr<T>>(static_cast<Self const*>(this));
    }

    constexpr auto as_ref() const noexcept -> ref<T> {
        return rstd::from_raw_parts<ref<T>>(static_cast<Self const*>(this));
    }

    constexpr auto as_mut_ref() const noexcept -> mut_ref<T>
        requires Mutable
    {
        return rstd::from_raw_parts<mut_ref<T>>(static_cast<Self const*>(this));
    }

    constexpr operator value_type*() const noexcept { return static_cast<Self const*>(this)->p; }

    constexpr void reset() noexcept {
        auto self = static_cast<Self const*>(this);
        self->p   = nullptr;
        if constexpr (meta::DSTArray<T>) {
            self->length = 0;
        }
    }

    /// \name Normal
    /// @{
    static constexpr auto from_raw_parts(value_type* p) noexcept -> Self
        requires(! meta::DST<T>) && meta::is_aggregate_v<Self>
    {
        return { .p = p };
    }
    /// @}

    /// \name Requires: DSTArray
    /// @{
    constexpr auto operator[](usize i) const noexcept
        requires meta::DSTArray<T>
    {
        return *(static_cast<Self const*>(this)->p + i);
    }

    constexpr auto len() const noexcept
        requires meta::DSTArray<T>
    {
        return static_cast<Self const*>(this)->length;
    }

    static constexpr auto from_raw_parts(value_type* p, usize length) noexcept -> Self
        requires meta::DSTArray<T> && meta::is_aggregate_v<Self>
    {
        return { .p = p, .length = length };
    }
    /// @}
};

template<typename T>
struct ref : ref_base<ref<T>, T, false> {
    static_assert(! meta::is_const_v<T>);

    T const* p { nullptr };

    using Self = ref;
};

template<meta::DSTArray T>
struct ref<T> : ref_base<ref<T>, T, false> {
    static_assert(! meta::is_const_v<T>);
    using value_type    = meta::remove_extent_t<T>;
    using metadata_type = usize;

    value_type const* p { nullptr };
    metadata_type     length;

    using Self = ref;
};

template<typename T>
struct mut_ref : ref_base<mut_ref<T>, T, true> {
    static_assert(! meta::is_const_v<T>);
    using Self = mut_ref;

    T* p { nullptr };
};

template<meta::DSTArray T>
struct mut_ref<T> : ref_base<mut_ref<T>, T, true> {
    static_assert(! meta::is_const_v<T>);
    using value_type = meta::remove_extent_t<T>;
    using Self       = mut_ref;

    value_type* p { nullptr };
    usize       length;
};

template<typename T>
struct ptr : ptr_base<ptr<T>, T, false> {
    static_assert(! meta::is_const_v<T>);
    using Self = ptr;

    T const* p { nullptr };
};

template<meta::DSTArray T>
struct ptr<T> : ptr_base<ptr<T>, T, false> {
    static_assert(! meta::is_const_v<T>);
    using value_type = meta::remove_extent_t<T>;
    using Self       = ptr;

    value_type const* p { nullptr };
    usize             length;
};

template<typename T>
struct mut_ptr : ptr_base<mut_ptr<T>, T, true> {
    static_assert(! meta::is_const_v<T>);
    using Self = mut_ptr;

    T* p { nullptr };
};

template<meta::DSTArray T>
struct mut_ptr<T> : ptr_base<mut_ptr<T>, T, true> {
    static_assert(! meta::is_const_v<T>);
    using value_type = meta::remove_extent_t<T>;
    using Self       = mut_ptr;

    value_type* p { nullptr };
    usize       length;
};

export template<typename T>
using slice = ref<T[]>;

export template<typename To, typename From>
struct AsCast {
    static_assert(false);
};

export template<typename To, typename From>
    requires requires() { typename AsCast<To, meta::remove_reference_t<From>>; }
[[gnu::always_inline]] inline constexpr auto as_cast(From&& from) noexcept -> To {
    return AsCast<To, meta::remove_reference_t<From>>::cast(rstd::forward<From>(from));
}

template<typename T>
struct AsCast<mut_ptr<T>, ptr<T>> {
    static constexpr auto cast(auto&& from) noexcept -> mut_ptr<T> {
        return rstd::from_raw_parts_override<mut_ptr<T>>(
            &from, const_cast<meta::remove_extent_t<T>*>(from.p));
    }
};

template<typename To>
struct AsCast<To*, u8*> {
    static constexpr auto cast(u8* from) noexcept -> To* { return reinterpret_cast<To*>(from); }
};
template<typename To>
struct AsCast<To*, u8 const*> {
    static constexpr auto cast(u8 const* from) noexcept -> To* {
        return reinterpret_cast<To*>(from);
    }
};

template<typename From>
struct AsCast<u8*, From*> {
    static constexpr auto cast(u8* from) noexcept -> u8* { return reinterpret_cast<u8*>(from); }
};
template<typename From>
struct AsCast<u8 const*, From*> {
    static constexpr auto cast(From* from) noexcept -> u8 const* {
        return reinterpret_cast<u8 const*>(from);
    }
};

} // namespace rstd
