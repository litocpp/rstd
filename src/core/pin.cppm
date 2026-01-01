export module rstd.core:pin;
export import :meta;

namespace rstd::pin
{

export template<class Ptr>
class Pin {
    Ptr pointer;

    constexpr Pin() noexcept = default;

public:
    constexpr explicit Pin(const Pin& p) noexcept(meta::is_nothrow_move_constructible_v<Ptr>)
        : pointer(p.pointer) {}

    template<typename T>
    static constexpr Pin make(T p) noexcept(meta::is_nothrow_move_constructible_v<Ptr>) {
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