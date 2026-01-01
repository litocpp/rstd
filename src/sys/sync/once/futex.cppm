export module rstd.sys:sync.once.futex;
import :pal;
import rstd.core;

namespace rstd::sys::sync::once::futex
{
using Primitive = pal::Primitive;
using Futex     = pal::Futex;

constexpr Primitive INCOMPLETE = 0;
constexpr Primitive POISONED   = 1;
constexpr Primitive RUNNING    = 2;
constexpr Primitive COMPLETE   = 3;
constexpr Primitive QUEUED     = 4;
constexpr Primitive STATE_MASK = 0x11;

class OnceState {
public:
    OnceState(): poisoned(false), set_state_to(INCOMPLETE) {}

    bool is_poisoned() const { return poisoned; }

    void poison() { set_state_to = POISONED; }

private:
    bool  poisoned;
    Futex set_state_to;
};

class CompletionGuard {
public:
    CompletionGuard(Futex* state_and_queued, Primitive set_state_on_drop_to);
    ~CompletionGuard();

private:
    Futex*    state_and_queued;
    Primitive set_state_on_drop_to;
};

class Once {
public:
    Once();

    bool is_completed() const;
    void wait(bool ignore_poisoning);
    // void call(bool ignore_poisoning, Dyn<FnMut, void(OnceState&)> f);

private:
    Futex state_and_queued;
};
} // namespace rstd