export module rstd.core:pin;
export import :basic;
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
    constexpr explicit Pin(const Pin& p) noexcept: pointer(p.pointer) {}
    constexpr explicit Pin(Pin&& p) noexcept(meta::is_nothrow_move_constructible_v<Ptr>)
        : pointer(rstd::move(p.pointer)) {}

    static constexpr Pin make(Ptr p) noexcept(meta::is_nothrow_move_constructible_v<Ptr> ||
                                              meta::is_trivially_copy_constructible_v<Ptr>) {
        return Pin { p };
    }

    // Construct a Pin without checking pinning guarantees (unsafe in Rust).
    static constexpr Pin
    make_unchecked(Ptr p) noexcept(meta::is_nothrow_move_constructible_v<Ptr>) {
        return Pin { p };
    }

    // Unwrap the Pin and return the inner pointer (unsafe in Rust).
    static constexpr Ptr
    into_inner_unchecked(Pin p) noexcept(meta::is_nothrow_move_constructible_v<Ptr>) {
        return p.pointer;
    }

    // Helpers to access the pointee. These assume Ptr is pointer-like and defines operator*.
    using pointee_ref_t = decltype(*rstd::declval<Ptr&>());
    using pointee_t     = meta::remove_reference_t<pointee_ref_t>;

    const pointee_t* get_ref() const noexcept { return rstd::addressof(*pointer); }
    pointee_t*       get_mut() noexcept { return rstd::addressof(*pointer); }
    pointee_t*       get_unchecked_mut() noexcept { return rstd::addressof(*pointer); }
};

} // namespace rstd::pin