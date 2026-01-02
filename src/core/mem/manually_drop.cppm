export module rstd.core:mem.manually_drop;
export import :core;
export import :meta;

namespace rstd::mem
{

template<typename T = void>
class ManuallyDrop {
    friend class ManuallyDrop<void>;
    alignas(T) rstd::byte storage[sizeof(T)];

public:
    ManuallyDrop(T&& v) { rstd::construct_at(storage, rstd::forward<T>(v)); }
    ~ManuallyDrop() = default;
    constexpr T*       operator->() noexcept { return reinterpret_cast<T*>(storage); }
    constexpr const T* operator->() const noexcept { return reinterpret_cast<const T*>(storage); }
    constexpr T&       operator*() noexcept { return *reinterpret_cast<T*>(storage); }
    constexpr const T& operator*() const noexcept { return *reinterpret_cast<const T*>(storage); }
};

template<typename T>
    requires(! meta::is_trivially_copy_constructible_v<T>)
class ManuallyDrop<T> {
    friend class ManuallyDrop<void>;
    alignas(T) rstd::byte storage[sizeof(T)];

public:
    ManuallyDrop(T&& v) noexcept(meta::is_nothrow_copy_constructible_v<T>) {
        rstd::construct_at(storage, rstd::forward<T>(v));
    }
    ManuallyDrop(const ManuallyDrop& o) noexcept(meta::is_nothrow_copy_constructible_v<T>) {
        rstd::construct_at(storage, *o);
    }
    ManuallyDrop(ManuallyDrop&& o) noexcept(meta::is_nothrow_move_constructible_v<T>) {
        rstd::construct_at(storage, rstd::move(*o));
    }
    ManuallyDrop& operator=(const ManuallyDrop& o) noexcept(meta::is_nothrow_copy_assignable_v<T>) {
        rstd::destroy_at(rstd::addressof(*this));
        rstd::construct_at(storage, rstd::move(*o));
    }
    ManuallyDrop& operator=(ManuallyDrop&& o) noexcept(meta::is_nothrow_move_assignable_v<T>) {
        rstd::destroy_at(rstd::addressof(*this));
        rstd::construct_at(storage, rstd::move(*o));
    }
    ~ManuallyDrop() = default;

    constexpr T*       operator->() noexcept { return reinterpret_cast<T*>(storage); }
    constexpr const T* operator->() const noexcept { return reinterpret_cast<const T*>(storage); }
    constexpr T&       operator*() noexcept { return *reinterpret_cast<T*>(storage); }
    constexpr const T& operator*() const noexcept { return *reinterpret_cast<const T*>(storage); }
};

template<>
class ManuallyDrop<void> {
public:
    template<typename T>
    static void drop(ManuallyDrop<T>&& d) {
        rstd::destroy_at(rstd::addressof(*d));
    }
};

} // namespace rstd::mem