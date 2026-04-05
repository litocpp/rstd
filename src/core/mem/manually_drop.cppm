export module rstd.core:mem.manually_drop;
export import :core;

namespace rstd::mem::manually_drop
{
template<typename T = void>
struct ManuallyDropData {
    alignas(T) rstd::byte storage[sizeof(T)] {};

    constexpr auto storage_loc() noexcept { return reinterpret_cast<T*>(rstd::addressof(storage)); }
    constexpr auto storage_loc() const noexcept {
        return reinterpret_cast<T const*>(rstd::addressof(storage));
    }
};

/// A wrapper that inhibits the automatic destructor call, analogous to Rust's `ManuallyDrop<T>`.
///
/// The contained value will not be dropped when `ManuallyDrop` goes out of scope.
/// Use `ManuallyDrop<void>::drop()` to explicitly destroy the inner value.
/// \tparam T The wrapped type; defaults to `void` for the factory/drop helper specialization.
export template<typename T = void>
class ManuallyDrop {
    friend class ManuallyDrop<void>;
    ManuallyDropData<T> d;

    constexpr ManuallyDrop(T&& v) noexcept {
        rstd::construct_at(d.storage_loc(), rstd::forward<T>(v));
    }

public:
    ~ManuallyDrop() = default;

    /// Creates a new `ManuallyDrop` wrapping the given value.
    /// \param v The value to wrap.
    /// \return A `ManuallyDrop` containing the moved value.
    [[nodiscard]]
    constexpr static auto make(T&& v) noexcept -> ManuallyDrop {
        return { rstd::forward<T>(v) };
    }

    constexpr T*       operator->() noexcept { return d.storage_loc(); }
    constexpr const T* operator->() const noexcept { return d.storage_loc(); }
    constexpr T&       operator*() noexcept { return *d.storage_loc(); }
    constexpr const T& operator*() const noexcept { return *d.storage_loc(); }

    /// Returns a const pointer to the contained value.
    constexpr auto as_ptr() const -> T const* { return d.storage_loc(); }

    /// Returns a mutable pointer to the contained value.
    constexpr auto as_mut_ptr() -> T* { return d.storage_loc(); }

    /// Extracts the contained value by move.
    constexpr auto take() -> T&& { return { rstd::move(**this) }; }
};

template<typename T>
    requires(! mtp::triv_copy<T>)
class ManuallyDrop<T> {
    friend class ManuallyDrop<void>;
    ManuallyDropData<T> d;

    constexpr ManuallyDrop(T&& v) noexcept(mtp::noex_move<T>) {
        rstd::construct_at(d.storage_loc(), rstd::forward<T>(v));
    }

public:
    constexpr static auto make(T&& v) noexcept(mtp::noex_move<T>)
        -> ManuallyDrop {
        return { rstd::forward<T>(v) };
    }
    constexpr ManuallyDrop(const ManuallyDrop& o) noexcept(
        mtp::noex_copy<T>) {
        rstd::construct_at(d.storage_loc(), *o);
    }
    constexpr ManuallyDrop(ManuallyDrop&& o) noexcept(mtp::noex_move<T>) {
        rstd::construct_at(d.storage_loc(), rstd::move(*o));
    }
    constexpr ManuallyDrop&
    operator=(const ManuallyDrop& o) noexcept(mtp::noex_assign_copy<T>) {
        rstd::destroy_at(d.storage_loc());
        rstd::construct_at(d.storage_loc(), rstd::move(*o));
    }
    constexpr ManuallyDrop&
    operator=(ManuallyDrop&& o) noexcept(mtp::noex_assign_move<T>) {
        rstd::destroy_at(d.storage_loc());
        rstd::construct_at(d.storage_loc(), rstd::move(*o));
    }
    ~ManuallyDrop() = default;

    constexpr T*       operator->() noexcept { return d.storage_loc(); }
    constexpr const T* operator->() const noexcept { return d.storage_loc(); }
    constexpr T&       operator*() noexcept { return *d.storage_loc(); }
    constexpr const T& operator*() const noexcept { return *d.storage_loc(); }

    constexpr auto as_ptr() const -> T const* { return d.storage_loc(); }
    constexpr auto as_mut_ptr() -> T* { return d.storage_loc(); }
    constexpr auto take() -> T && { return { rstd::move(**this) }; }
};

template<>
class ManuallyDrop<void> {
public:
    template<typename T>
    static auto make(T&& d) {
        static_assert(! mtp::is_ref<T>);
        return ManuallyDrop<T>::make(rstd::forward<T>(d));
    }

    template<typename T>
    static void drop(ManuallyDrop<T>&& d) {
        rstd::destroy_at(rstd::addressof(*d));
    }
};

} // namespace rstd::mem::manually_drop