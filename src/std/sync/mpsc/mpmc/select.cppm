module;
#include <cassert>
export module rstd:sync.mpsc.mpmc.select;
export import rstd.core;

namespace rstd::sync::mpsc::mpmc
{

static_assert(sizeof(rstd::uintptr_t) <= sizeof(rstd::size_t));

/// Identifier associated with an operation by a specific thread on a specific channel.
export struct Operation {
    usize val;

    bool operator==(const Operation&) const = default;

    template<typename T>
    static Operation hook(T* r) {
        auto const pointer = reinterpret_cast<rstd::uintptr_t>(r);
        assert(pointer > 2);
        return Operation { usize(pointer) };
    }
};

/// Current state of a blocking operation.
export struct Selected {
    enum class State : rstd::size_t
    {
        Waiting      = 0,
        Aborted      = 1,
        Disconnected = 2,
        Operation    = 3, // Actual operation uses values > 2
    };

    usize val;

    static constexpr auto state_value(State state) noexcept -> usize {
        return usize(static_cast<rstd::size_t>(state));
    }

    static Selected Waiting() { return Selected { state_value(State::Waiting) }; }
    static Selected Aborted() { return Selected { state_value(State::Aborted) }; }
    static Selected Disconnected() { return Selected { state_value(State::Disconnected) }; }
    static Selected Op(Operation oper) { return Selected { oper.val }; }

    bool operator==(const Selected&) const = default;

    bool is_waiting() const { return val == state_value(State::Waiting); }
    bool is_aborted() const { return val == state_value(State::Aborted); }
    bool is_disconnected() const { return val == state_value(State::Disconnected); }

    Option<Operation> operation() const {
        if (val > state_value(State::Disconnected)) {
            return Some(Operation { val });
        }
        return None();
    }

                    operator usize() const { return val; }
    static Selected from_usize(usize v) { return Selected { v }; }
};

} // namespace rstd::sync::mpsc::mpmc
