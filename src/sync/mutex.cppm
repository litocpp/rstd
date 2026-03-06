module;
#include <rstd/macro.hpp>
export module rstd:sync.mutex;
export import :sys;
export import rstd.core;

namespace rstd::sync
{

export template<typename T>
class MutexGuard {
    sys::sync::mutex::Mutex* m_lock;
    T* m_data;

public:
    MutexGuard(sys::sync::mutex::Mutex* l, T* d) : m_lock(l), m_data(d) {
        m_lock->lock();
    }

    ~MutexGuard() {
        if (m_lock) m_lock->unlock();
    }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

    MutexGuard(MutexGuard&& other) noexcept : m_lock(other.m_lock), m_data(other.m_data) {
        other.m_lock = nullptr;
    }

    MutexGuard& operator=(MutexGuard&& other) noexcept {
        if (this != &other) {
            if (m_lock) m_lock->unlock();
            m_lock = other.m_lock;
            m_data = other.m_data;
            other.m_lock = nullptr;
        }
        return *this;
    }

    auto operator*() -> T& { return *m_data; }
    auto operator->() -> T* { return m_data; }
};

export template<typename T>
class Mutex {
    mutable sys::sync::mutex::Mutex m_lock;
    mutable T m_data;

public:
    Mutex(T initial_data) : m_lock(sys::sync::mutex::Mutex::make()), m_data(rstd::move(initial_data)) {}

    auto lock_mut() const -> Result<MutexGuard<T>, empty> {
        return Ok<MutexGuard<T>, empty>(MutexGuard<T>(&m_lock, &m_data));
    }

    // Simplified for now, no poisoning
    auto lock() const -> Result<MutexGuard<T>, empty> {
        return lock_mut();
    }
};

} // namespace rstd::sync