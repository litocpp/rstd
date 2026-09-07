module rstd;
import :sys.sync.thread_parking.darwin;

using rstd::sync::atomic::Ordering;

namespace rstd::sys::sync::thread_parking::darwin
{

using DispatchTime = rstd::uint64_t;

inline constexpr DispatchTime DISPATCH_TIME_NOW {};
inline constexpr DispatchTime DISPATCH_TIME_FOREVER = ~DispatchTime {};

extern "C" auto dispatch_time(DispatchTime when, rstd::int64_t delta) -> DispatchTime;
extern "C" auto dispatch_semaphore_create(rstd::intptr_t value) -> void*;
extern "C" auto dispatch_semaphore_wait(void* semaphore, DispatchTime timeout) -> rstd::intptr_t;
extern "C" auto dispatch_semaphore_signal(void* semaphore) -> rstd::intptr_t;
extern "C" void dispatch_release(void* object);

Parker::Parker(): semaphore(dispatch_semaphore_create(0)), state(EMPTY) {
    if (semaphore == nullptr) {
        rstd::panic { "failed to create dispatch semaphore for thread synchronization" };
    }
}

Parker::~Parker() {
    dispatch_release(semaphore);
}

void Parker::park() {
    if (state.fetch_sub(i8(1), Ordering::Acquire) == NOTIFIED) return;

    while (dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER) != 0) {
    }
    state.exchange(EMPTY, Ordering::Acquire);
}

void Parker::park_timeout(rstd::time::Duration timeout) {
    if (state.fetch_sub(i8(1), Ordering::Acquire) == NOTIFIED) return;

    auto const nanos = timeout.as_nanos();
    auto const max   = u128(static_cast<rstd::uint128_t>(i64::MAX.to_primitive()));
    auto const delta =
        nanos > max ? i64::MAX.to_primitive() : static_cast<rstd::int64_t>(nanos.to_primitive());
    auto const deadline  = dispatch_time(DISPATCH_TIME_NOW, delta);
    auto const timed_out = dispatch_semaphore_wait(semaphore, deadline) != 0;

    auto const old = state.exchange(EMPTY, Ordering::Acquire);
    if (old == NOTIFIED && timed_out) {
        while (dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER) != 0) {
        }
    }
}

void Parker::unpark() {
    if (state.exchange(NOTIFIED, Ordering::Release) == PARKED) {
        dispatch_semaphore_signal(semaphore);
    }
}

} // namespace rstd::sys::sync::thread_parking::darwin
