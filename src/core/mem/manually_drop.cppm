module;
#include <memory>
#include <utility>
export module rstd.core:mem.manually_drop;
import :basic;
import :meta;

namespace rstd::mem
{

template<typename T = void>
class ManuallyDrop {
    friend class ManuallyDrop<void>;
    alignas(T) rstd::byte storage[sizeof(T)];

public:
    ManuallyDrop(T&& v) { std::construct_at(storage, std::forward<T>(v)); }
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
        std::construct_at(storage, std::forward<T>(v));
    }
    ManuallyDrop(const ManuallyDrop& o) noexcept(meta::is_nothrow_copy_constructible_v<T>) {
        std::construct_at(storage, *o);
    }
    ManuallyDrop(ManuallyDrop&& o) noexcept(meta::is_nothrow_move_constructible_v<T>) {
        std::construct_at(storage, std::move(*o));
    }
    ManuallyDrop& operator=(const ManuallyDrop& o) noexcept(meta::is_nothrow_copy_assignable_v<T>) {
        std::destroy_at(std::addressof(*this));
        std::construct_at(storage, std::move(*o));
    }
    ManuallyDrop& operator=(ManuallyDrop&& o) noexcept(meta::is_nothrow_move_assignable_v<T>) {
        std::destroy_at(std::addressof(*this));
        std::construct_at(storage, std::move(*o));
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
        std::destroy_at(std::addressof(*d));
    }
};

} // namespace rstd::mem