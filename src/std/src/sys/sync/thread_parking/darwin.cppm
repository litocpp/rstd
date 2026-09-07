export module rstd:sys.sync.thread_parking.darwin;
export import rstd.core;

namespace rstd::sys::sync::thread_parking::darwin
{

export class Parker {
private:
    static constexpr i8 EMPTY {};
    static constexpr i8 NOTIFIED { rstd::int8_t(1) };
    static constexpr i8 PARKED { rstd::int8_t(-1) };

    void*                          semaphore;
    rstd::sync::atomic::Atomic<i8> state;

public:
    Parker();
    ~Parker();

    Parker(const Parker&)            = delete;
    Parker(Parker&&)                 = delete;
    Parker& operator=(const Parker&) = delete;
    Parker& operator=(Parker&&)      = delete;

    void park();
    void park_timeout(rstd::time::Duration timeout);
    void unpark();
};

} // namespace rstd::sys::sync::thread_parking::darwin
