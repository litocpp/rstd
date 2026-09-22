export module rstd.core:mem.swap;
import :num.types;

export namespace rstd::mem
{

/// Exchanges objects without invoking assignment. Moves and destructors must not throw.
template<typename T>
    requires(! mtp::is_const<T>) && mtp::init<T, T&&>
constexpr void swap(T& a, T& b) noexcept {
    if (rstd::addressof(a) == rstd::addressof(b)) return;
    // Proxy assignment may write through instead of rebinding.
    T value(rstd::move(a));
    rstd::destroy_at(rstd::addressof(a));
    rstd::construct_at(rstd::addressof(a), rstd::move(b));
    rstd::destroy_at(rstd::addressof(b));
    rstd::construct_at(rstd::addressof(b), rstd::move(value));
}

} // namespace rstd::mem
