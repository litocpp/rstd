export module rstd:sys.sync.once.futex;
import :sys.pal;
import rstd.core;

namespace rstd::sys::sync::once::futex
{
using Primitive = pal::futex::Primitive;
using Futex     = pal::futex::Futex;

constexpr Primitive COMPLETE {};
constexpr Primitive RUNNING { static_cast<Primitive::primitive_type>(1) };
constexpr Primitive INCOMPLETE { static_cast<Primitive::primitive_type>(3) };
constexpr Primitive QUEUED { static_cast<Primitive::primitive_type>(4) };
constexpr Primitive STATE_MASK { static_cast<Primitive::primitive_type>(0b11) };

static_assert((COMPLETE & STATE_MASK) == COMPLETE);
static_assert((RUNNING & STATE_MASK) == RUNNING);
static_assert((INCOMPLETE & STATE_MASK) == INCOMPLETE);
static_assert((QUEUED & STATE_MASK) == Primitive {});

export enum class ExclusiveState {
    Incomplete,
    Complete,
};

export using Callback = void (*)(void*);

export class Once {
public:
    constexpr Once() noexcept: state_and_queued(INCOMPLETE) {}
    Once(Once const&)                    = delete;
    auto operator=(Once const&) -> Once& = delete;
    Once(Once&&)                         = delete;
    auto operator=(Once&&) -> Once&      = delete;

    auto is_completed() const noexcept -> bool;
    void wait() const;
    void call(void* context, Callback callback) const;

    auto state() & noexcept -> ExclusiveState;
    void set_state(ExclusiveState state) & noexcept;

private:
    mutable Futex state_and_queued;
};
} // namespace rstd::sys::sync::once::futex
