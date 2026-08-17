module rstd;

import :sys.sync.condvar.futex;

namespace rstd::sys::sync::condvar::futex
{

Condvar::Condvar() noexcept: m_futex(pal::futex::Primitive {}) {
}

auto Condvar::make() noexcept -> Condvar {
    return {};
}

void Condvar::notify_one() noexcept {
    m_futex.fetch_add(pal::futex::Primitive(static_cast<pal::futex::Primitive::primitive_type>(1)),
                      rstd::sync::atomic::Ordering::Relaxed);
    pal::futex::futex_wake(&m_futex);
}

void Condvar::notify_all() noexcept {
    m_futex.fetch_add(pal::futex::Primitive(static_cast<pal::futex::Primitive::primitive_type>(1)),
                      rstd::sync::atomic::Ordering::Relaxed);
    pal::futex::futex_wake_all(&m_futex);
}

void Condvar::wait(Mutex& mutex) noexcept {
    (void)wait_optional_timeout(mutex, None());
}

auto Condvar::wait_timeout(Mutex& mutex, rstd::time::Duration timeout) noexcept -> bool {
    return wait_optional_timeout(mutex, Some(timeout));
}

auto Condvar::wait_optional_timeout(Mutex& mutex, Option<rstd::time::Duration> timeout) noexcept
    -> bool {
    auto value = m_futex.load(rstd::sync::atomic::Ordering::Relaxed);

    mutex.unlock();
    auto notified = pal::futex::futex_wait(&m_futex, value, timeout);
    mutex.lock();

    return notified;
}

} // namespace rstd::sys::sync::condvar::futex
