export module rstd:sys.sync.once.queue;
import :thread.current;
export import rstd.core;

namespace rstd::sys::sync::once::queue
{

export enum class ExclusiveState {
    Incomplete,
    Complete,
};

export using Callback = void (*)(void*);

export class Once {
private:
    static constexpr rstd::uintptr_t COMPLETE {};
    static constexpr rstd::uintptr_t RUNNING    = 1;
    static constexpr rstd::uintptr_t INCOMPLETE = 3;
    static constexpr rstd::uintptr_t STATE_MASK = 0b11;

    mutable rstd::sync::atomic::Atomic<rstd::uintptr_t> state_and_queue;

public:
    constexpr Once() noexcept: state_and_queue(INCOMPLETE) {}

    Once(const Once&)            = delete;
    Once(Once&&)                 = delete;
    Once& operator=(const Once&) = delete;
    Once& operator=(Once&&)      = delete;

    auto is_completed() const noexcept -> bool;
    void wait() const;
    void call(void* context, Callback callback) const;

    auto state() & noexcept -> ExclusiveState;
    void set_state(ExclusiveState state) & noexcept;
};

} // namespace rstd::sys::sync::once::queue
