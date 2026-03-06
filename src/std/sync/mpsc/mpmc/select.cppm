module;
#include <cassert>
export module rstd:sync.mpsc.mpmc.select;
export import rstd.core;

namespace rstd::sync::mpsc::mpmc
{

/// Identifier associated with an operation by a specific thread on a specific channel.
export struct Operation {
    usize val;

    bool operator==(const Operation&) const = default;

    template<typename T>
    static Operation hook(T* r) {
        auto v = reinterpret_cast<usizeptr>(r);
        assert(v > 2);
        return Operation { v };
    }
};

/// Current state of a blocking operation.
export struct Selected {
    enum class State : usize {
        Waiting      = 0,
        Aborted      = 1,
        Disconnected = 2,
        Operation    = 3, // Actual operation uses values > 2
    };

    usize val;

    static Selected Waiting() { return Selected { static_cast<usize>(State::Waiting) }; }
    static Selected Aborted() { return Selected { static_cast<usize>(State::Aborted) }; }
    static Selected Disconnected() { return Selected { static_cast<usize>(State::Disconnected) }; }
    static Selected Op(Operation oper) { return Selected { oper.val }; }

    bool operator==(const Selected&) const = default;

    bool is_waiting() const { return val == static_cast<usize>(State::Waiting); }
    bool is_aborted() const { return val == static_cast<usize>(State::Aborted); }
    bool is_disconnected() const { return val == static_cast<usize>(State::Disconnected); }

    Option<Operation> operation() const {
        if (val > 2) {
            return Some(Operation { val });
        }
        return None();
    }

    operator usize() const { return val; }
    static Selected from_usize(usize v) { return Selected { v }; }
};

} // namespace rstd::sync::mpsc::mpmc