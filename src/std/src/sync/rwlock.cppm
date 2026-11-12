module;
#include <rstd/macro.hpp>
export module rstd:sync.rwlock;
export import :sync.condvar;

namespace rstd::sync
{
export template<typename T>
class RwLock;

export template<typename T>
class RwLockReadGuard {
    const RwLock<T>* lock_;
    friend class RwLock<T>;
    explicit RwLockReadGuard(const RwLock<T>* lock): lock_(lock) {}

public:
    USE_TRAIT(RwLockReadGuard)
    RwLockReadGuard(const RwLockReadGuard&)                    = delete;
    auto operator=(const RwLockReadGuard&) -> RwLockReadGuard& = delete;
    RwLockReadGuard(RwLockReadGuard&& other) noexcept
        : lock_(rstd::exchange(other.lock_, nullptr)) {}
    auto operator=(RwLockReadGuard&& other) noexcept -> RwLockReadGuard& {
        if (this != &other) {
            if (lock_) lock_->release_read();
            lock_ = rstd::exchange(other.lock_, nullptr);
        }
        return *this;
    }
    ~RwLockReadGuard() {
        if (lock_) lock_->release_read();
    }
    auto deref() const noexcept -> ref<T> { return ref<T>::from_raw_parts(&lock_->data_); }
};

export template<typename T>
class RwLockWriteGuard {
    const RwLock<T>* lock_;
    friend class RwLock<T>;
    explicit RwLockWriteGuard(const RwLock<T>* lock): lock_(lock) {}

public:
    USE_TRAIT(RwLockWriteGuard)
    RwLockWriteGuard(const RwLockWriteGuard&)                    = delete;
    auto operator=(const RwLockWriteGuard&) -> RwLockWriteGuard& = delete;
    RwLockWriteGuard(RwLockWriteGuard&& other) noexcept
        : lock_(rstd::exchange(other.lock_, nullptr)) {}
    auto operator=(RwLockWriteGuard&& other) noexcept -> RwLockWriteGuard& {
        if (this != &other) {
            if (lock_) lock_->release_write();
            lock_ = rstd::exchange(other.lock_, nullptr);
        }
        return *this;
    }
    ~RwLockWriteGuard() {
        if (lock_) lock_->release_write();
    }
    auto deref() const noexcept -> ref<T> { return ref<T>::from_raw_parts(&lock_->data_); }
    auto deref_mut() noexcept -> mut_ref<T> { return mut_ref<T>::from_raw_parts(&lock_->data_); }
};

/// A reader-preferred lock. Readers may nest while a writer is waiting; writers may starve.
/// Guards borrow the lock. Write recursion and read-to-write upgrades are not supported.
export template<typename T>
class RwLock {
    struct State {
        usize readers {};
        bool  writer {};
    };
    Mutex<State>    state_;
    mutable Condvar changed_;
    mutable T       data_;
    friend class RwLockReadGuard<T>;
    friend class RwLockWriteGuard<T>;

    void release_read() const {
        auto state = state_.lock().unwrap();
        --state->readers;
        if (state->readers == usize()) changed_.notify_all();
    }
    void release_write() const {
        auto state    = state_.lock().unwrap();
        state->writer = false;
        changed_.notify_all();
    }

public:
    template<typename... Args>
    explicit RwLock(Args&&... args): data_(rstd::forward<Args>(args)...) {}
    RwLock(const RwLock&)                    = delete;
    auto operator=(const RwLock&) -> RwLock& = delete;
    RwLock(RwLock&&)                         = delete;
    auto operator=(RwLock&&) -> RwLock&      = delete;

    auto read() const [[clang::lifetimebound]] -> Result<RwLockReadGuard<T>, empty> {
        auto state = state_.lock().unwrap();
        while (state->writer) changed_.wait(state);
        if (state->readers == usize::MAX) panic { "RwLock reader count exhausted" };
        ++state->readers;
        return Ok(RwLockReadGuard<T>(this));
    }
    auto write() const [[clang::lifetimebound]] -> Result<RwLockWriteGuard<T>, empty> {
        auto state = state_.lock().unwrap();
        while (state->writer || state->readers != usize()) changed_.wait(state);
        state->writer = true;
        return Ok(RwLockWriteGuard<T>(this));
    }
    auto try_read() const [[clang::lifetimebound]] -> Result<RwLockReadGuard<T>, TryLockError> {
        auto acquired = state_.try_lock();
        if (acquired.is_err()) return Err(TryLockError::WouldBlock);
        auto state = rstd::move(acquired).unwrap();
        if (state->writer) return Err(TryLockError::WouldBlock);
        if (state->readers == usize::MAX) panic { "RwLock reader count exhausted" };
        ++state->readers;
        return Ok(RwLockReadGuard<T>(this));
    }
    auto try_write() const [[clang::lifetimebound]] -> Result<RwLockWriteGuard<T>, TryLockError> {
        auto acquired = state_.try_lock();
        if (acquired.is_err()) return Err(TryLockError::WouldBlock);
        auto state = rstd::move(acquired).unwrap();
        if (state->writer || state->readers != usize()) return Err(TryLockError::WouldBlock);
        state->writer = true;
        return Ok(RwLockWriteGuard<T>(this));
    }
};
} // namespace rstd::sync

namespace rstd
{
template<typename T>
struct Impl<ops::Deref, sync::RwLockReadGuard<T>> : ImplBase<sync::RwLockReadGuard<T>> {
    using Target = T;
    auto deref() const noexcept -> ref<T> { return this->self().deref(); }
};
template<typename T>
struct Impl<ops::Deref, sync::RwLockWriteGuard<T>> : ImplBase<sync::RwLockWriteGuard<T>> {
    using Target = T;
    auto deref() const noexcept -> ref<T> { return this->self().deref(); }
};
template<typename T>
struct Impl<ops::DerefMut, sync::RwLockWriteGuard<T>> : ImplBase<sync::RwLockWriteGuard<T>> {
    auto deref_mut() noexcept -> mut_ref<T> { return this->self().deref_mut(); }
};
} // namespace rstd
