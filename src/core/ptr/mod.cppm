export module rstd.core:ptr;
export import :ptr.non_null;
export import :ptr.unique;
export import :ptr.metadata;
export import :ptr.dyn;
export import :ptr.ptr;

namespace rstd::ptr_
{

/// Creates a null mutable raw pointer.
export template<typename T>
[[gnu::always_inline]] inline constexpr auto null_mut() noexcept -> T* {
    return nullptr;
}

/// Creates a pointer with the given address
export template<typename T>
[[gnu::always_inline]] inline auto without_provenance_mut(usize addr) noexcept -> T* {
    // An int-to-pointer transmute currently has exactly the intended semantics: it creates a
    // pointer without provenance. Note that this is *not* a stable guarantee about transmute
    // semantics, it relies on sysroot crates having special status.
    // SAFETY: every valid integer is also a valid pointer (as long as you don't dereference that
    // pointer).
    return rstd::bit_cast<T*>(addr);
}

} // namespace rstd::ptr_