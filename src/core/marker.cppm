export module rstd.core:marker;
export import :trait;

namespace rstd
{

/// Marker trait for types that can be duplicated by simple bitwise copy.
export struct Copy {};

/// Marker trait for types that can be safely transferred across thread boundaries.
export struct Send {};

/// Marker trait for types that can be safely shared between threads via references.
export struct Sync {};

/// Marker trait for types with a constant size known at compile time.
export struct Sized {};

} // namespace rstd

export namespace rstd
{

template<typename T>
struct Impl<Sized, T[]> {
    ~Impl() = delete;
};

template<typename T>
    requires mtp::drop<T>
struct Impl<Sized, T> {};

template<typename T>
    requires mtp::triv_copy<T> && mtp::triv_assign_copy<T>
struct Impl<Copy, T> {};

static_assert(Impled<i32, Sized>);
static_assert(! Impled<i32[], Sized>);

} // namespace rstd
