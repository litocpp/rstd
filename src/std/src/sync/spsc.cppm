module;
#include <rstd/enum.hpp>

export module rstd:sync.spsc;
export import rstd.core;
import rstd.alloc;

namespace rstd::sync::spsc
{
using atomic::Atomic;
using atomic::Ordering;

export class CreateError {
    RSTD_ENUM(CreateError, (InvalidCapacity), (AllocationFailed))
};
export class ReadError {
    RSTD_ENUM(ReadError, (Empty), (Disconnected))
};
export template<typename T>
class PushError {
    RSTD_ENUM(PushError, (Full, (T value;)), (Disconnected, (T value;)))
};

// Cursors wrap at twice the capacity, so arbitrary capacities never depend on integer overflow.
constexpr auto next_cursor(size_t cursor, size_t capacity) noexcept -> size_t {
    return cursor + 1 == capacity * 2 ? 0 : cursor + 1;
}
constexpr auto slot_index(size_t cursor, size_t capacity) noexcept -> size_t {
    return cursor < capacity ? cursor : cursor - capacity;
}
constexpr auto opposite_cursor(size_t cursor, size_t capacity) noexcept -> size_t {
    return cursor < capacity ? cursor + capacity : cursor - capacity;
}

template<typename T, typename A>
struct Storage {
    static_assert(__atomic_always_lock_free(sizeof(size_t), nullptr));
    static_assert(__atomic_always_lock_free(sizeof(bool), nullptr));
    static_assert(noexcept(T(rstd::declval<T&&>())), "SPSC elements must be nothrow movable");
    static_assert(noexcept(rstd::declval<T&>().~T()), "SPSC elements must be nothrow destructible");
    static_assert(noexcept(A(rstd::declval<A&&>())), "SPSC allocators must be nothrow movable");

    alignas(64) Atomic<size_t> read {};
    alignas(64) Atomic<size_t> write {};
    Atomic<bool>         producer_open { true }, consumer_open { true }, released { false };
    bool                 borrowed {};
    size_t               capacity;
    alloc::Layout        layout;
    mem::MaybeUninit<T>* slots;
    A                    allocator;

    Storage(size_t cap, alloc::Layout allocation, mem::MaybeUninit<T>* values, A resource)
        : capacity(cap), layout(allocation), slots(values), allocator(rstd::move(resource)) {}

    void release() noexcept {
        if (! released.exchange(true, Ordering::AcqRel)) return;
        auto       cursor = read.load(Ordering::Relaxed);
        const auto end    = write.load(Ordering::Relaxed);
        while (cursor != end) {
            slots[slot_index(cursor, capacity)].assume_init_drop();
            cursor = next_cursor(cursor, capacity);
        }
        for (size_t i = 0; i < capacity; ++i) rstd::destroy_at(slots + i);
        auto resource   = rstd::move(allocator);
        auto allocation = layout;
        rstd::destroy_at(this);
        as<alloc::Allocator>(resource).deallocate(this, allocation);
    }
    void ensure_unborrowed() const {
        if (borrowed) rstd::panic { "SPSC consumer has an active read guard" };
    }
};

export template<typename T, typename A = ::alloc::Global>
class Producer;
export template<typename T, typename A = ::alloc::Global>
class Consumer;
export template<typename T, typename A = ::alloc::Global>
class RingBuffer;

// A guard borrows its Consumer. Destroy it before moving or closing that endpoint.
export template<typename T, typename A = ::alloc::Global>
class ReadGuard {
    Storage<T, A>* state_ {};
    explicit ReadGuard(Storage<T, A>* state): state_(state) { state_->borrowed = true; }
    friend class Consumer<T, A>;
    void release_borrow() noexcept {
        if (state_) state_->borrowed = false;
        state_ = nullptr;
    }

public:
    ReadGuard(const ReadGuard&)                    = delete;
    auto operator=(const ReadGuard&) -> ReadGuard& = delete;
    ReadGuard(ReadGuard&& other) noexcept: state_(rstd::exchange(other.state_, nullptr)) {}
    auto operator=(ReadGuard&& other) noexcept -> ReadGuard& {
        if (this != &other) {
            release_borrow();
            state_ = rstd::exchange(other.state_, nullptr);
        }
        return *this;
    }
    ~ReadGuard() { release_borrow(); }
    auto operator*() const -> const T& {
        if (! state_) rstd::panic { "SPSC read guard is no longer active" };
        return state_->slots[slot_index(state_->read.load(Ordering::Relaxed), state_->capacity)]
            .assume_init_ref();
    }
    auto operator->() const -> const T* { return rstd::addressof(**this); }
    void consume() {
        if (! state_) rstd::panic { "SPSC read guard was already consumed" };
        auto cursor = state_->read.load(Ordering::Relaxed);
        state_->slots[slot_index(cursor, state_->capacity)].assume_init_drop();
        state_->read.store(next_cursor(cursor, state_->capacity), Ordering::Release);
        release_borrow();
    }
};

// Each endpoint permits calls from only one thread at a time. Moves transfer that responsibility.
export template<typename T, typename A>
class Producer {
    Storage<T, A>* state_ {};
    explicit Producer(Storage<T, A>* state): state_(state) {}
    friend class RingBuffer<T, A>;

public:
    Producer()                                   = default;
    Producer(const Producer&)                    = delete;
    auto operator=(const Producer&) -> Producer& = delete;
    Producer(Producer&& other) noexcept: state_(rstd::exchange(other.state_, nullptr)) {}
    auto operator=(Producer&& other) noexcept -> Producer& {
        if (this != &other) {
            close();
            state_ = rstd::exchange(other.state_, nullptr);
        }
        return *this;
    }
    ~Producer() { close(); }
    void close() noexcept {
        if (auto* state = rstd::exchange(state_, nullptr)) {
            state->producer_open.store(false, Ordering::Release);
            state->release();
        }
    }
    auto capacity() const noexcept -> usize { return usize(state_ ? state_->capacity : 0); }
    auto is_disconnected() const noexcept -> bool {
        return ! state_ || ! state_->consumer_open.load(Ordering::Acquire);
    }
    auto is_empty() const noexcept -> bool {
        return ! state_ ||
               state_->read.load(Ordering::Acquire) == state_->write.load(Ordering::Relaxed);
    }
    auto is_full() const noexcept -> bool {
        return state_ && opposite_cursor(state_->write.load(Ordering::Relaxed), state_->capacity) ==
                             state_->read.load(Ordering::Acquire);
    }
    // A push already in flight may succeed while the consumer closes; its value is reclaimed later.
    auto try_push(T value) -> Result<empty, PushError<T>> {
        if (is_disconnected()) return Err(PushError<T>::Disconnected(rstd::move(value)));
        auto cursor = state_->write.load(Ordering::Relaxed);
        if (opposite_cursor(cursor, state_->capacity) == state_->read.load(Ordering::Acquire))
            return Err(PushError<T>::Full(rstd::move(value)));
        state_->slots[slot_index(cursor, state_->capacity)].write(rstd::move(value));
        state_->write.store(next_cursor(cursor, state_->capacity), Ordering::Release);
        return Ok(empty {});
    }
};

export template<typename T, typename A>
class Consumer {
    Storage<T, A>* state_ {};
    explicit Consumer(Storage<T, A>* state): state_(state) {}
    friend class RingBuffer<T, A>;

public:
    Consumer()                                   = default;
    Consumer(const Consumer&)                    = delete;
    auto operator=(const Consumer&) -> Consumer& = delete;
    Consumer(Consumer&& other) noexcept {
        if (other.state_) other.state_->ensure_unborrowed();
        state_ = rstd::exchange(other.state_, nullptr);
    }
    auto operator=(Consumer&& other) noexcept -> Consumer& {
        if (this != &other) {
            if (other.state_) other.state_->ensure_unborrowed();
            close();
            state_ = rstd::exchange(other.state_, nullptr);
        }
        return *this;
    }
    ~Consumer() { close(); }
    void close() noexcept {
        if (! state_) return;
        state_->ensure_unborrowed();
        auto* state = rstd::exchange(state_, nullptr);
        state->consumer_open.store(false, Ordering::Release);
        state->release();
    }
    auto capacity() const noexcept -> usize { return usize(state_ ? state_->capacity : 0); }
    auto is_disconnected() const noexcept -> bool {
        return ! state_ || ! state_->producer_open.load(Ordering::Acquire);
    }
    auto is_empty() const noexcept -> bool {
        return ! state_ ||
               state_->read.load(Ordering::Relaxed) == state_->write.load(Ordering::Acquire);
    }
    auto try_front() -> Result<ReadGuard<T, A>, ReadError> {
        if (! state_) return Err(ReadError::Disconnected());
        state_->ensure_unborrowed();
        if (is_empty()) {
            if (! is_disconnected()) return Err(ReadError::Empty());
            // Acquire the final published cursor after observing producer shutdown.
            if (is_empty()) return Err(ReadError::Disconnected());
        }
        return Ok(ReadGuard<T, A>(state_));
    }
    auto try_pop() -> Result<T, ReadError> {
        auto front = try_front();
        if (front.is_err()) return Err(rstd::move(front).unwrap_err());
        auto guard  = rstd::move(front).unwrap();
        auto cursor = state_->read.load(Ordering::Relaxed);
        auto value =
            T(rstd::move(state_->slots[slot_index(cursor, state_->capacity)].assume_init_mut()));
        guard.consume();
        return Ok(rstd::move(value));
    }
};

export template<typename T, typename A>
class RingBuffer {
public:
    // The allocator is stored by value; any resource it borrows must outlive both endpoints.
    static auto make(usize capacity, A allocator = A {})
        -> Result<tuple<Producer<T, A>, Consumer<T, A>>, CreateError> {
        using State = Storage<T, A>;
        using Slot  = mem::MaybeUninit<T>;
        auto cap    = capacity.to_primitive();
        if (! cap || cap > usize::MAX.to_primitive() / 2)
            return Err(CreateError::InvalidCapacity());
        auto slots_layout = alloc::Layout::array<Slot>(capacity);
        if (slots_layout.is_none()) return Err(CreateError::InvalidCapacity());
        usize offset;
        auto  combined = alloc::Layout::make<State>().extend(*slots_layout, offset);
        if (combined.is_none() || combined->size > usize(isize::MAX.to_primitive()))
            return Err(CreateError::InvalidCapacity());
        auto layout     = *combined;
        auto allocation = as<alloc::Allocator>(allocator).allocate(layout);
        if (allocation.is_err()) return Err(CreateError::AllocationFailed());
        auto* raw   = rstd::move(allocation).unwrap().pointer;
        auto* slots = reinterpret_cast<Slot*>(static_cast<byte*>(raw) + offset.to_primitive());
        for (size_t i = 0; i < cap; ++i) rstd::construct_at(slots + i);
        auto* state =
            rstd::construct_at(static_cast<State*>(raw), cap, layout, slots, rstd::move(allocator));
        return Ok(
            tuple<Producer<T, A>, Consumer<T, A>>(Producer<T, A>(state), Consumer<T, A>(state)));
    }
};
} // namespace rstd::sync::spsc
