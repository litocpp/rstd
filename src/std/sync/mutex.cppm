module;
#include <rstd/macro.hpp>
export module rstd:sync.mutex;
export import :sys.sync.mutex;
export import rstd.core;

using sys_mutex_t = rstd::sys::sync::mutex::Mutex;

namespace rstd::sync
{

/// An RAII guard returned by `Mutex::lock`, providing access to the protected data.
///
/// The mutex is released when this guard is dropped.
/// \tparam T The type of the data protected by the mutex.
export template<typename T>
class MutexGuard {
    sys_mutex_t* m_lock;
    T*           m_data;

public:
    MutexGuard(sys_mutex_t* l, T* d): m_lock(l), m_data(d) { m_lock->lock(); }

    ~MutexGuard() {
        if (m_lock) m_lock->unlock();
    }

    MutexGuard(const MutexGuard&)            = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

    MutexGuard(MutexGuard&& other) noexcept: m_lock(other.m_lock), m_data(other.m_data) {
        other.m_lock = nullptr;
    }

    MutexGuard& operator=(MutexGuard&& other) noexcept {
        if (this != &other) {
            if (m_lock) m_lock->unlock();
            m_lock       = other.m_lock;
            m_data       = other.m_data;
            other.m_lock = nullptr;
        }
        return *this;
    }

    auto operator*() -> T& { return *m_data; }
    auto operator->() -> T* { return m_data; }
};

/// A mutual exclusion primitive useful for protecting shared data.
/// \tparam T The type of the data protected by this mutex.
export template<typename T>
class Mutex {
    mutable sys_mutex_t m_lock;
    mutable T           m_data;

public:
    /// Creates a new mutex wrapping the given data.
    /// \param initial_data The initial value to protect.
    Mutex(T initial_data): m_lock(sys_mutex_t::make()), m_data(rstd::move(initial_data)) {}

    /// Acquires the mutex, blocking the current thread until it is able to do so.
    /// \return A MutexGuard providing mutable access to the protected data.
    auto lock_mut() const -> Result<MutexGuard<T>, empty> {
        return Ok<MutexGuard<T>, empty>(MutexGuard<T>(&m_lock, &m_data));
    }

    /// Acquires the mutex (alias for lock_mut; no poisoning support yet).
    auto lock() const -> Result<MutexGuard<T>, empty> { return lock_mut(); }
};

} // namespace rstd::sync