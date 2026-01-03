export module rstd.core:mem.manually_drop;
export import :core;
export import :meta;

namespace rstd::mem::manually_drop
{
template<typename T = void>
struct ManuallyDropData {
    alignas(T) rstd::byte storage[sizeof(T)];

    constexpr auto storage_loc() noexcept { return reinterpret_cast<T*>(rstd::addressof(storage)); }
    constexpr auto storage_loc() const noexcept {
        return reinterpret_cast<T const*>(rstd::addressof(storage));
    }
};

export template<typename T = void>
class ManuallyDrop {
    friend class ManuallyDrop<void>;
    ManuallyDropData<T> self;

    constexpr ManuallyDrop(T&& v) noexcept {
        rstd::construct_at(self.storage_loc(), rstd::forward<T>(v));
    }

public:
    ~ManuallyDrop() = default;
    constexpr static auto make(T&& v) noexcept -> ManuallyDrop { return { rstd::forward<T>(v) }; }

    constexpr T*       operator->() noexcept { return self.storage_loc(); }
    constexpr const T* operator->() const noexcept { return self.storage_loc(); }
    constexpr T&       operator*() noexcept { return *self.storage_loc(); }
    constexpr const T& operator*() const noexcept { return *self.storage_loc(); }
};

export template<typename T>
    requires(! meta::is_trivially_copy_constructible_v<T>)
class ManuallyDrop<T> {
    friend class ManuallyDrop<void>;
    ManuallyDropData<T> self;

    constexpr ManuallyDrop(T&& v) noexcept(meta::is_nothrow_move_constructible_v<T>) {
        rstd::construct_at(self.storage_loc(), rstd::forward<T>(v));
    }

public:
    constexpr static auto make(T&& v) noexcept(meta::is_nothrow_move_constructible_v<T>)
        -> ManuallyDrop {
        return { rstd::forward<T>(v) };
    }
    constexpr ManuallyDrop(const ManuallyDrop& o) noexcept(
        meta::is_nothrow_copy_constructible_v<T>) {
        rstd::construct_at(self.storage_loc(), *o);
    }
    constexpr ManuallyDrop(ManuallyDrop&& o) noexcept(meta::is_nothrow_move_constructible_v<T>) {
        rstd::construct_at(self.storage_loc(), rstd::move(*o));
    }
    constexpr ManuallyDrop&
    operator=(const ManuallyDrop& o) noexcept(meta::is_nothrow_copy_assignable_v<T>) {
        rstd::destroy_at(self.storage_loc());
        rstd::construct_at(self.storage_loc(), rstd::move(*o));
    }
    constexpr ManuallyDrop&
    operator=(ManuallyDrop&& o) noexcept(meta::is_nothrow_move_assignable_v<T>) {
        rstd::destroy_at(self.storage_loc());
        rstd::construct_at(self.storage_loc(), rstd::move(*o));
    }
    ~ManuallyDrop() = default;

    constexpr T*       operator->() noexcept { return self.storage_loc(); }
    constexpr const T* operator->() const noexcept { return self.storage_loc(); }
    constexpr T&       operator*() noexcept { return *self.storage_loc(); }
    constexpr const T& operator*() const noexcept { return *self.storage_loc(); }
};

export template<>
class ManuallyDrop<void> {
public:
    template<typename T>
    static auto make(T&& d) {
        static_assert(! meta::is_reference_v<T>);
        return ManuallyDrop<T>::make(rstd::forward<T>(d));
    }

    template<typename T>
    static void drop(ManuallyDrop<T>&& d) {
        rstd::destroy_at(rstd::addressof(*d));
    }
};

} // namespace rstd::mem::manually_drop