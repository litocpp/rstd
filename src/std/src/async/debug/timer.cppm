export module rstd:async.debug.timer;
import :async.debug;

using namespace rstd;
using namespace rstd::literals;

namespace rstd::async::debug
{

class TimerObservation {
    Recorder record_;

public:
    TimerObservation(): record_(current().child(Kind::Operation, { "timer"_str })) {
        record_.state(State::Submitted);
    }

    TimerObservation(const TimerObservation&)                    = delete;
    auto operator=(const TimerObservation&) -> TimerObservation& = delete;
    ~TimerObservation() { record_.state(State::Released, true); }

    void ready(bool failed) const {
        record_.outcome(failed ? Outcome::Error : Outcome::Success);
        record_.state(State::Ready);
    }

    void cancel() const {
        record_.cancel();
        record_.state(State::Cancelled);
    }

    auto target_id() const -> Id { return record_.target_id(); }
};

} // namespace rstd::async::debug
