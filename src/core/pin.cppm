export module rstd.core:pin;
export import :core;

namespace rstd::pin
{

/// A pinned pointer that guarantees the pointee will not be moved, analogous to Rust's `Pin<P>`.
///
/// Pinning prevents the pointee from being moved out of its location in memory,
/// which is required for self-referential types and certain async patterns.
/// \tparam Ptr The pointer type being pinned (e.g., `mut_ptr<T>`, a smart pointer).
export template<class Ptr>
class Pin {
    Ptr pointer;

    constexpr Pin() noexcept = default;
    constexpr explicit Pin(rstd::param_ref_t<Ptr> p) noexcept
        : pointer(rstd::param_forward<Ptr>(p)) {}

public:
    constexpr Pin(const Pin& p) noexcept
        requires(mtp::triv_copy<Ptr>)
        : pointer(p.pointer) {}
    constexpr Pin(Pin&& p) noexcept(mtp::noex_move<Ptr>)
        : pointer(rstd::move(p.pointer)) {}

    constexpr Pin& operator=(Pin&& p) noexcept(mtp::noex_assign_move<Ptr>) {
        pointer = rstd::move(p.pointer);
        return *this;
    }

    /// Pins a pointer, asserting that the pointee satisfies pinning requirements.
    /// \param p The pointer to pin.
    /// \return A `Pin` wrapping the pointer.
    static Pin make(Ptr p) noexcept(mtp::noex_move<Ptr> ||
                                    mtp::triv_copy<Ptr>) {
        return Pin { p };
    }

    /// Constructs a `Pin` without verifying pinning guarantees (unsafe).
    /// \param p The pointer to pin.
    /// \return A `Pin` wrapping the pointer.
    static constexpr Pin
    make_unchecked(Ptr p) noexcept(mtp::noex_move<Ptr> ||
                                   mtp::triv_copy<Ptr>) {
        return Pin { p };
    }

    /// Unwraps the `Pin`, returning the inner pointer (unsafe).
    /// \param p The pinned pointer to unwrap.
    /// \return The inner pointer.
    static constexpr Ptr
    into_inner_unchecked(Pin p) noexcept(mtp::noex_move<Ptr>) {
        return rstd::param_forward<Ptr>(p.pointer);
    }

    /// Returns an immutable reference to the inner pointer.
    const Ptr& get_ref() const noexcept { return pointer; }

    /// Returns a mutable reference to the inner pointer.
    Ptr&       get_mut() noexcept { return pointer; }

    /// Returns a mutable reference to the inner pointer without borrow checking (unsafe).
    Ptr&       get_unchecked_mut() noexcept { return pointer; }

    using pointee_ptr_t = mtp::add_ptr<mtp::rm_ref<Ptr>>;
    pointee_ptr_t operator->() { return rstd::addressof(get_unchecked_mut()); }
    auto          operator->() const { return rstd::addressof(get_ref()); }
};

} // namespace rstd::pin