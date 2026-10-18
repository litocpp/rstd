export module rstd.core:mem.maybe_uninit;
import :num.types;
export import :core;
export import :mem.manually_drop;

namespace rstd::mem::maybe_uninit
{

/// Traits for extracting the value type from a `MaybeUninit` specialization.
/// \tparam T The type to inspect; primary template yields `void`.
export template<typename T>
struct maybe_uninit_traits {
    using value_type = void;
};

/// Storage with T's size and alignment, without automatic construction or destruction of T.
/// Default construction and uninit() leave the bytes uninitialized; zeroed() only clears them.
/// The caller tracks whether a live, fully initialized T exists. No method checks this state.
/// Violating the lifetime or initialization preconditions of assume_init* is undefined behavior.
/// Use write() or placement construction through as_mut_ptr() to create T, then either
/// consume it with assume_init() or destroy it with assume_init_drop(), exactly once.
///
/// Copying or moving this wrapper copies storage bytes, not T. It does not transfer ownership
/// or replace T's copy/move constructor; do not use it to relocate a live nontrivial T.
/// Uninitialized storage must not be read as T. Compiler automatic-initialization options
/// and language rules still apply; this wrapper does not override them.
///
/// rstd treats the wrapper address as aligned byte storage without accessing its member.
/// Raw MaybeUninit<T>* pointers address one slot only; do not use them for pointer arithmetic.
/// Prefer array<MaybeUninit<T>, N> for owned slots and byte-backed mut_ptr/mut_ref views for borrows.
/// Native slot arrays are discouraged.
/// This storage convention is an implementation assumption, not a general C++ lifetime rule.
export template<typename T = void>
class MaybeUninit {
    alignas(T) rstd::byte storage[sizeof(T)];

public:
    // A user-provided constructor prevents value-initialization from zeroing the storage.
    constexpr MaybeUninit() noexcept {}

private:
    struct Zeroed {};
    constexpr explicit MaybeUninit(Zeroed) noexcept: storage {} {}

    constexpr auto storage_ptr() noexcept -> T* {
        return reinterpret_cast<T*>(reinterpret_cast<byte*>(this));
    }
    constexpr auto storage_ptr() const noexcept -> const T* {
        return reinterpret_cast<const T*>(reinterpret_cast<const byte*>(this));
    }

    constexpr explicit MaybeUninit(T&& val) noexcept(mtp::noex_move_v<T>) {
        rstd::construct_at(storage_ptr(), rstd::forward<T>(val));
    }

    constexpr T* ptr() noexcept { return rstd::launder(storage_ptr()); }

    constexpr const T* ptr() const noexcept { return rstd::launder(storage_ptr()); }

public:
    /// Ends the storage lifetime without invoking T's destructor.
    ~MaybeUninit() = default;

    /// Move-constructs T in the storage. The caller remains responsible for destroying it.
    constexpr static auto make(T&& val) noexcept(mtp::noex_move_v<T>) -> MaybeUninit {
        return MaybeUninit(rstd::forward<T>(val));
    }

    /// Creates storage without constructing T or initializing its bytes.
    constexpr static auto uninit() noexcept -> MaybeUninit {
#if __has_cpp_attribute(indeterminate)
        [[indeterminate]]
        MaybeUninit value;
#else
        MaybeUninit value;
#endif
        return value;
    }

    /// Clears the bytes without calling T's constructor. This does not guarantee a live, valid T;
    /// zero bits need not be a valid representation. Construct T before assuming it.
    constexpr static auto zeroed() noexcept -> MaybeUninit { return MaybeUninit(Zeroed {}); }

    /// Move-constructs T and returns a borrow tied to this storage.
    /// Does not destroy an existing T. Destroy an owned previous value first to avoid leaks.
    /// If construction throws, no new T exists; the slot may be constructed again.
    constexpr auto write(T&& val) & noexcept(mtp::noex_move_v<T>) [[clang::lifetimebound]] -> T& {
        return *rstd::construct_at(storage_ptr(), rstd::forward<T>(val));
    }

    /// Returns the storage address; accessing T requires its lifetime and initialization.
    constexpr auto as_ptr() const noexcept -> const T* { return storage_ptr(); }

    /// May be used as the destination of placement construction before T exists.
    constexpr auto as_mut_ptr() noexcept -> T* { return storage_ptr(); }

    /// Consumes a live, fully initialized T by move-constructing the result and destroying
    /// the source T. The slot contains no live T afterward, including when the move throws.
    /// Do not read or drop it again without constructing another T.
    /// Unlike Rust's move, this runs T's C++ move constructor and source destructor.
    constexpr auto assume_init() && noexcept(mtp::noex_move_v<T> && mtp::noex_drop_v<T>) -> T
        requires(mtp::move<T>)
    {
        struct Source {
            T* value;
            constexpr ~Source() noexcept(mtp::noex_drop_v<T>) { rstd::destroy_at(value); }
        } source { ptr() };
        return T(rstd::move(*source.value));
    }

    /// Copies a live, fully initialized T, leaving the source alive and owned by this slot.
    /// Uses T's copy constructor, not Rust's unchecked bitwise read. If copying throws, the
    /// source remains alive and must still be destroyed by the caller.
    constexpr auto assume_init_read() const noexcept(mtp::noex_copy_v<T>) -> T
        requires(mtp::copy<T>)
    {
        return *ptr();
    }

    /// Borrows a live, fully initialized T. The borrow ends before T is destroyed or replaced,
    /// or this storage ceases to exist. This does not initialize T or extend its lifetime.
    constexpr auto assume_init_ref() const& noexcept [[clang::lifetimebound]] -> const T& {
        return *ptr();
    }
    constexpr auto assume_init_ref() const&& -> const T& = delete;

    /// Mutably borrows a live, fully initialized T, with the same lifetime rules as assume_init_ref().
    /// This cannot be used to initialize storage that does not yet contain T.
    constexpr auto assume_init_mut() & noexcept [[clang::lifetimebound]] -> T& { return *ptr(); }

    /// Destroys a live, fully initialized T exactly once, leaving the slot available for reuse.
    /// No contained T may be accessed again until a new T is constructed.
    constexpr void assume_init_drop() noexcept(mtp::noex_drop_v<T>) { rstd::destroy_at(ptr()); }
};

template<typename T>
struct maybe_uninit_traits<MaybeUninit<T>> {
    using value_type = T;
};

} // namespace rstd::mem::maybe_uninit
