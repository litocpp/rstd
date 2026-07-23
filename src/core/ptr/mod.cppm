export module rstd.core:ptr;
import :num.types;
export import :ptr.non_null;
export import :ptr.metadata;
export import :ptr.dyn;
export import :ptr.ptr;

namespace rstd::ptr_
{

/// Copies `count` elements from `source` to non-overlapping storage at `destination`.
/// Both ranges must be valid and suitably aligned for `T`.
export template<typename T>
    requires(! mtp::DST<T>)
void copy_nonoverlapping(ptr<T> source, mut_ptr<T> destination, usize count) {
    if (count == usize()) return;
    using Storage    = typename ptr<T>::storage_type;
    auto const bytes = count * usize(sizeof(Storage));
    __builtin_memcpy(destination.as_raw_ptr(), source.as_raw_ptr(), bytes.to_primitive());
}

/// Constructs one logical element in uninitialized storage.
export template<typename T, typename... Args>
    requires(! mtp::DST<T>)
constexpr void construct(mut_ptr<T> destination, Args&&... args) {
    if constexpr (mtp::same_as<T, u8>) {
        auto value                  = u8(rstd::forward<Args>(args)...);
        *destination.as_raw_ptr() = value.to_byte();
    } else {
        rstd::construct_at(destination.as_raw_ptr(), rstd::forward<Args>(args)...);
    }
}

/// Destroys one logical element without deallocating its storage.
export template<typename T>
    requires(! mtp::DST<T>)
constexpr void destroy(mut_ptr<T> destination) noexcept {
    if constexpr (! mtp::same_as<T, u8>) rstd::destroy_at(destination.as_raw_ptr());
}

/// Moves one logical element out of initialized storage.
export template<typename T>
    requires(! mtp::DST<T>)
constexpr auto move_out(mut_ptr<T> source) -> T {
    if constexpr (mtp::same_as<T, u8>) {
        return u8::from_byte(*source.as_raw_ptr());
    } else {
        return rstd::move(*source.as_raw_ptr());
    }
}

/// Assigns one logical element in initialized storage.
export template<typename T, typename U>
    requires(! mtp::DST<T>)
constexpr void write(mut_ptr<T> destination, U&& value) {
    if constexpr (mtp::same_as<T, u8>) {
        *destination.as_raw_ptr() = u8(rstd::forward<U>(value)).to_byte();
    } else {
        *destination.as_raw_ptr() = rstd::forward<U>(value);
    }
}

/// Creates a null mutable raw pointer.
/// \tparam T The pointee type.
/// \return A null pointer of type `T*`.
export template<typename T>
[[gnu::always_inline]]
inline constexpr auto null_mut() noexcept -> T* {
    return nullptr;
}

/// Creates a mutable pointer with the given address and no provenance.
/// \tparam T The pointee type.
/// \param addr The address for the pointer.
/// \return A pointer to `T` with the given address.
export template<typename T>
[[gnu::always_inline]]
inline auto without_provenance_mut(usize addr) noexcept -> T* {
    // An int-to-pointer transmute currently has exactly the intended semantics: it creates a
    // pointer without provenance. Note that this is *not* a stable guarantee about transmute
    // semantics, it relies on sysroot crates having special status.
    // SAFETY: every valid integer is also a valid pointer (as long as you don't dereference that
    // pointer).
    return rstd::bit_cast<T*>(addr);
}

} // namespace rstd::ptr_
