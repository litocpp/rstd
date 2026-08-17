module rstd;

import :sys.sync.mutex.pthread;
import rstd.alloc;

using rstd_alloc::boxed::Box;

namespace rstd::sys::sync::mutex::pthread
{

Mutex::Mutex() noexcept: m_pal(OnceBox<pal::Mutex>::make()) {
}

auto Mutex::make() -> Mutex {
    return {};
}

void Mutex::lock() {
    get().lock();
}

bool Mutex::try_lock() {
    return get().try_lock();
}

void Mutex::unlock() {
    get().unlock();
}

auto Mutex::pal_mutex() -> pal::Mutex& {
    return get();
}

Mutex::~Mutex() {
    if (! m_pal) return;
    if (auto value = m_pal.take()) {
        auto& native = *value;
        if (native->try_lock()) {
            native->unlock();
        } else {
            auto leaked = rstd::move(native).into_raw();
            (void)leaked;
        }
    }
}

auto Mutex::get() -> pal::Mutex& {
    return m_pal.get_or_init([]() -> Box<pal::Mutex> {
        return Box<pal::Mutex>::make();
    });
}

} // namespace rstd::sys::sync::mutex::pthread
