export module rstd:sys.sync.mutex.pthread;

export import :sys.sync.once_box;
export import :sys.pal;

namespace rstd::sys::sync::mutex::pthread
{

export class Mutex {
    OnceBox<pal::Mutex> m_pal;

    Mutex() noexcept;

public:
    static auto make() -> Mutex;
    void        lock();
    bool        try_lock();
    void        unlock();
    auto        pal_mutex() -> pal::Mutex&;
    ~Mutex();

private:
    auto get() -> pal::Mutex&;
};

} // namespace rstd::sys::sync::mutex::pthread
