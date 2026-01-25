export module rstd.core:mem.maybe_uninit;
export import :core;
export import :meta;
export import :mem.manually_drop;

namespace rstd::mem::maybe_uninit
{

export template<typename T>
struct maybe_uninit_traits {
    using value_type = void;
};

/// A wrapper type to construct uninitialized instances of `T`.
///
/// This is useful for constructing uninitialized data that is later
/// initialized. Unlike direct use of uninitialized memory, `MaybeUninit<T>`
/// signals to the compiler that the data here might *not* be initialized.
///
/// Note that dropping a `MaybeUninit<T>` will never call `T`'s destructor.
/// It is your responsibility to make sure `T` gets destroyed if it got initialized.
export template<typename T = void>
class MaybeUninit {
    alignas(T) rstd::byte storage[sizeof(T)];

    constexpr MaybeUninit() noexcept = default;

    constexpr explicit MaybeUninit(T&& val) noexcept(meta::is_nothrow_move_constructible_v<T>) {
        rstd::construct_at(ptr(), rstd::forward<T>(val));
    }

    constexpr T* ptr() noexcept {
        return rstd::launder(reinterpret_cast<T*>(rstd::addressof(storage)));
    }

    constexpr const T* ptr() const noexcept {
        return rstd::launder(reinterpret_cast<const T*>(rstd::addressof(storage)));
    }

public:
    /// Destructor does nothing - it never calls T's destructor
    ~MaybeUninit() = default;

    /// Creates a new `MaybeUninit<T>` initialized with the given value.
    /// It is safe to call `assume_init()` on the return value of this function.
    ///
    /// Note that dropping a `MaybeUninit<T>` will never call `T`'s destructor.
    /// It is your responsibility to make sure `T` gets destroyed if it got initialized.
    constexpr static auto make(T&& val) noexcept(meta::is_nothrow_move_constructible_v<T>)
        -> MaybeUninit {
        return MaybeUninit(rstd::forward<T>(val));
    }

    /// Creates a new `MaybeUninit<T>` in an uninitialized state.
    ///
    /// Note that dropping a `MaybeUninit<T>` will never call `T`'s destructor.
    /// It is your responsibility to make sure `T` gets destroyed if it got initialized.
    constexpr static auto uninit() noexcept -> MaybeUninit { return MaybeUninit(); }

    /// Creates a new `MaybeUninit<T>` in an uninitialized state, with the memory being
    /// filled with `0` bytes.
    ///
    /// Note that if `T` has padding bytes, those bytes may not be zeroed.
    /// It depends on `T` whether that already makes for proper initialization.
    constexpr static auto zeroed() noexcept -> MaybeUninit {
        MaybeUninit m;
        for (usize i = 0; i < sizeof(T); ++i) {
            m.storage[i] = rstd::byte { 0 };
        }
        return m;
    }

    /// Sets the value of the `MaybeUninit<T>`.
    ///
    /// This overwrites any previous value without dropping it, so be careful
    /// not to use this twice unless you want to skip running the destructor.
    /// For your convenience, this also returns a mutable reference to the
    /// (now safely initialized) contents.
    constexpr auto write(T&& val) noexcept(meta::is_nothrow_move_constructible_v<T>) -> T& {
        rstd::construct_at(ptr(), rstd::forward<T>(val));
        return *ptr();
    }

    /// Gets a const pointer to the contained value.
    /// Reading from this pointer or dereferencing it is undefined behavior
    /// unless the `MaybeUninit<T>` is initialized.
    constexpr auto as_ptr() const noexcept -> const T* { return ptr(); }

    /// Gets a mutable pointer to the contained value.
    /// Reading from this pointer or dereferencing it is undefined behavior
    /// unless the `MaybeUninit<T>` is initialized.
    constexpr auto as_mut_ptr() noexcept -> T* { return ptr(); }

    /// Extracts the value from the `MaybeUninit<T>` container by move.
    ///
    /// # Safety
    ///
    /// It is up to the caller to guarantee that the `MaybeUninit<T>` really is in an
    /// initialized state. Calling this when the content is not yet fully initialized
    /// causes undefined behavior.
    constexpr auto assume_init() && noexcept -> T
        requires(meta::is_move_constructible_v<T>)
    {
        T result = rstd::move(*ptr());
        return result;
    }

    /// Extracts the value from the `MaybeUninit<T>` container by copy.
    ///
    /// # Safety
    ///
    /// It is up to the caller to guarantee that the `MaybeUninit<T>` really is in an
    /// initialized state. Calling this when the content is not yet fully initialized
    /// causes undefined behavior.
    constexpr auto assume_init_read() const noexcept -> T
        requires(meta::is_copy_constructible_v<T>)
    {
        return *ptr();
    }

    /// Gets a const reference to the contained value.
    ///
    /// # Safety
    ///
    /// Calling this when the content is not yet fully initialized causes undefined
    /// behavior: it is up to the caller to guarantee that the `MaybeUninit<T>` really
    /// is in an initialized state.
    constexpr auto assume_init_ref() const noexcept -> const T& { return *ptr(); }

    /// Gets a mutable reference to the contained value.
    ///
    /// # Safety
    ///
    /// Calling this when the content is not yet fully initialized causes undefined
    /// behavior: it is up to the caller to guarantee that the `MaybeUninit<T>` really
    /// is in an initialized state.
    constexpr auto assume_init_mut() noexcept -> T& { return *ptr(); }

    /// Drops the contained value in place.
    ///
    /// # Safety
    ///
    /// It is up to the caller to guarantee that the `MaybeUninit<T>` really is
    /// in an initialized state. Calling this when the content is not yet fully
    /// initialized causes undefined behavior.
    constexpr void assume_init_drop() noexcept { rstd::destroy_at(ptr()); }
};

template<typename T>
struct maybe_uninit_traits<MaybeUninit<T>> {
    using value_type = T;
};

} // namespace rstd::mem::maybe_uninit
