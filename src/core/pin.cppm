export module rstd.core:pin;
export import :core;
export import :meta;

namespace rstd::pin
{

export template<class Ptr>
class Pin {
    Ptr pointer;

    constexpr Pin() noexcept = default;
    constexpr explicit Pin(rstd::param_ref_t<Ptr> p) noexcept
        : pointer(rstd::param_forward<Ptr>(p)) {}

public:
    constexpr Pin(const Pin& p) noexcept
        requires(meta::is_trivially_copy_constructible_v<Ptr>)
        : pointer(p.pointer) {}
    constexpr Pin(Pin&& p) noexcept(meta::is_nothrow_move_constructible_v<Ptr>)
        : pointer(rstd::move(p.pointer)) {}

    static Pin make(Ptr p) noexcept(meta::is_nothrow_move_constructible_v<Ptr> ||
                                    meta::is_trivially_copy_constructible_v<Ptr>) {
        return Pin { p };
    }

    // Construct a Pin without checking pinning guarantees (unsafe in Rust).
    static constexpr Pin
    make_unchecked(Ptr p) noexcept(meta::is_nothrow_move_constructible_v<Ptr> ||
                                   meta::is_trivially_copy_constructible_v<Ptr>) {
        return Pin { p };
    }

    // Unwrap the Pin and return the inner pointer (unsafe in Rust).
    static constexpr Ptr
    into_inner_unchecked(Pin p) noexcept(meta::is_nothrow_move_constructible_v<Ptr>) {
        return rstd::param_forward<Ptr>(p.pointer);
    }

    const Ptr& get_ref() const noexcept { return pointer; }
    Ptr&       get_mut() noexcept { return pointer; }
    Ptr&       get_unchecked_mut() noexcept { return pointer; }

    using pointee_ptr_t = meta::add_pointer_t<meta::remove_reference_t<Ptr>>;
    pointee_ptr_t operator->() { return rstd::addressof(get_unchecked_mut()); }
    auto          operator->() const { return rstd::addressof(get_ref()); }
};

} // namespace rstd::pin