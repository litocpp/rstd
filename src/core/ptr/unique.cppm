export module rstd.core:ptr.unique;
export import :ptr.non_null;

namespace rstd::ptr_::unique
{

using non_null::NonNull;

export template<typename T>
struct Unique {
private:
    NonNull<T> pointer;

    constexpr Unique(NonNull<T> p): pointer(p) {}

public:
    constexpr Unique(const Unique&)            = delete;
    constexpr Unique& operator=(const Unique&) = delete;

    constexpr Unique(Unique&& o) noexcept: pointer(o.pointer) {
        o.pointer = NonNull<T>::make_unchecked(nullptr);
    }
    constexpr Unique& operator=(Unique&& o) noexcept {
        pointer   = o.pointer;
        o.pointer = NonNull<T>::make_unchecked(nullptr);
        return *this;
    }

    static constexpr auto make_unchecked(ptr<T> p) -> Unique {
        return { NonNull<T>::make_unchecked(p) };
    }

    constexpr auto as_ptr() const -> ptr<T> { return pointer.as_ptr(); }

    constexpr bool operator==(ptr<T> in) const noexcept { return pointer == in; }
    constexpr bool operator==(nullptr_t in) const noexcept { return pointer == in; }
};

} // namespace rstd::ptr_::unique