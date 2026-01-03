export module rstd.core:marker;
export import :trait;

namespace rstd
{

export struct Copy {};

export struct Send {};

export struct Sync {};

/// Types with a constant size known at compile time.
export struct Sized {};

} // namespace rstd

export namespace rstd
{

template<typename T>
struct Impl<Sized, T[]> {
    ~Impl() = delete;
};

template<typename T>
    requires meta::destructible<T>
struct Impl<Sized, T> {};

template<typename T>
    requires meta::is_trivially_copy_constructible_v<T> && meta::is_trivially_copy_assignable_v<T>
struct Impl<Copy, T> {};

static_assert(Impled<i32, Sized>);
static_assert(! Impled<i32[], Sized>);

} // namespace rstd
