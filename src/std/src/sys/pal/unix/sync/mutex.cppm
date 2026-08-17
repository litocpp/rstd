export module rstd:sys.pal.unix.sync.mutex;

import :sys.libc.pthread;
import rstd.core;

using namespace rstd::sys::libc;

namespace rstd::sys::pal::unix::sync::mutex
{

export class Mutex {
    pthread_mutex_t inner;

public:
    Mutex() noexcept;
    ~Mutex() noexcept;

    Mutex(const Mutex&)            = delete;
    Mutex(Mutex&&)                 = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex& operator=(Mutex&&)      = delete;

    static auto make() noexcept -> Mutex;
    auto        raw() noexcept -> pthread_mutex_t*;
    void        lock() noexcept;
    auto        try_lock() noexcept -> bool;
    void        unlock() noexcept;
};

static_assert(mtp::triv_copy<mut_ptr<Mutex>>);

} // namespace rstd::sys::pal::unix::sync::mutex
