module;
#include <rstd/macro.hpp>
export module rstd.core:mem.drop_guard;
export import :cmp;
export import :mem.manually_drop;

using rstd::mem::manually_drop::ManuallyDrop;

namespace rstd::mem::drop_guard
{
export template<typename T, typename F>
struct DropGuard {
    ManuallyDrop<T> inner;
    ManuallyDrop<F> f;

    USE_TRAIT(DropGuard)

    ~DropGuard() {
        auto inner = rstd::move(this->inner).take();
        auto f     = this->f.take();
        f(rstd::addressof(inner));
    }

    explicit DropGuard(T&& inner, F&& f)
        : inner(ManuallyDrop<T>::make(rstd::move(inner))),
          f(ManuallyDrop<F>::make(rstd::move(f))) {}

    static auto make(T&& inner, F&& f) -> Self {
        return { .inner = ManuallyDrop<T>::make(rstd::move(inner)),
                 .f     = ManuallyDrop<F>::make(rstd::move(f)) };
    }

    constexpr auto       operator->() noexcept { return as_ptr(); }
    constexpr const auto operator->() const noexcept { return as_ptr(); }

    constexpr auto as_ptr(this Self const& self) noexcept { return self.inner.as_ptr(); }
    constexpr auto as_mut_ptr(this Self& self) noexcept { return self.inner.as_ptr(); }
};
} // namespace rstd::mem::drop_guard