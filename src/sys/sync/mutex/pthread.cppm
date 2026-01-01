export module rstd.sys:sync.mutex.pthread;
export import :sync.once_box;
export import :pal;
export import :lib.pthread;
export import rstd.alloc;

using rstd::boxed::Box;
using rstd::sys::sync::OnceBox;
using namespace rstd::sys::lib::pthread;

namespace rstd::sys::sync::mutex::pthread
{

export class Mutex {
    OnceBox<pal::Mutex> pal;

    Mutex() noexcept: pal(OnceBox<pal::Mutex>::make()) {}

public:
    static auto make() -> Mutex { return {}; }

    void lock() { get()->lock(); }

    bool try_lock() { return get()->try_lock(); }

    void unlock() { get()->unlock(); }

    ~Mutex() {
        if (! pal) return;
        if (auto opt = pal.take()) {
            auto& p = opt->get_unchecked_mut();
            if (p->try_lock()) {
                p->unlock();
            } else {
                // Locked: leak the underlying pthread_mutex_t to mirror Rust behavior.
                auto leaked = p.into_raw();
                (void)leaked;
            }
        }
    }

private:
    auto get() -> Pin<pal::Mutex&> {
        return pal.get_or_init([]() -> Pin<Box<pal::Mutex>> {
            auto pal = Box<pal::Mutex>::pin(pal::Mutex::make());
            return pal;
        });
    }
};

} // namespace rstd::sys::sync::mutex::pthread