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
    requires meta::semiregular<T>
struct Impl<Sized, T> {};

template<typename T>
    requires meta::is_trivially_copy_constructible_v<T> && meta::is_trivially_copy_assignable_v<T>
struct Impl<Copy, T> {};
} // namespace rstd
