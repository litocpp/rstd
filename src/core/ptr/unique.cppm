module;
#include <rstd/macro.hpp>
export module rstd.core:ptr.unique;
export import :ptr.non_null;

namespace rstd::ptr_::unique
{

using non_null::NonNull;

export template<typename T>
struct Unique {
private:
    /// use Option here, as no linear type
    /// also define `operator bool` for Unique
    Option<NonNull<T>> pointer;

    constexpr Unique(Option<NonNull<T>> p): pointer(p) {}

public:
    USE_TRAIT(Unique)

    constexpr Unique(const Unique&)            = delete;
    constexpr Unique& operator=(const Unique&) = delete;

    constexpr Unique(Unique&& o) noexcept: pointer(o.pointer) { o.pointer = {}; }
    constexpr Unique& operator=(Unique&& o) noexcept {
        pointer   = o.pointer;
        o.pointer = {};
        return *this;
    }
    constexpr bool operator==(ptr<T> in) const noexcept { return pointer && *pointer == in; }
    constexpr bool operator==(nullptr_t in) const noexcept { return ! pointer || *pointer == in; }
    constexpr      operator bool() const noexcept { return pointer.is_some(); }

    void reset() noexcept { pointer = {}; }

    /// \name ?Sized
    /// @{
    static constexpr auto make_unchecked(mut_ptr<T> p) -> Unique {
        return { Some(NonNull<T>::make_unchecked(p)) };
    }

    constexpr auto as_ptr() const -> ptr<T> { return pointer->as_ptr(); }
    constexpr auto as_mut_ptr() -> mut_ptr<T> { return pointer->as_mut_ptr(); }
    /// @}

    /// \name Sized
    /// @{
    static auto dangling() noexcept -> Self
        requires Impled<T, Sized>
    {
        return make_unchecked(mut_ptr<T>::from_raw_parts(nullptr));
    }
    /// @}
};

} // namespace rstd::ptr_::unique