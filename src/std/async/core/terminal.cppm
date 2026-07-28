export module rstd:async.terminal;
import rstd.core;

using namespace rstd;

enum class TerminalCellState
{
    Pending,
    Ready,
    Consumed,
    Closed,
};

template<typename T>
class TerminalCell {
    TerminalCellState m_state { TerminalCellState::Pending };
    Option<T>         m_value;

public:
    auto is_pending() const noexcept -> bool { return m_state == TerminalCellState::Pending; }
    auto is_ready() const noexcept -> bool { return m_state == TerminalCellState::Ready; }
    auto is_consumed() const noexcept -> bool { return m_state == TerminalCellState::Consumed; }
    auto is_closed() const noexcept -> bool { return m_state == TerminalCellState::Closed; }
    auto is_terminal() const noexcept -> bool { return ! is_pending(); }

    auto publish(T value) -> bool {
        if (! is_pending()) {
            return false;
        }
        m_value.insert(rstd::move(value));
        m_state = TerminalCellState::Ready;
        return true;
    }

    auto take() -> T {
        if (! is_ready() || m_value.is_none()) {
            rstd::panic { "TerminalCell::take called without a ready value" };
        }
        m_state = TerminalCellState::Consumed;
        return rstd::move(m_value).unwrap_unchecked();
    }

    void close() {
        if (is_consumed()) {
            return;
        }
        m_value = None();
        m_state = TerminalCellState::Closed;
    }
};
