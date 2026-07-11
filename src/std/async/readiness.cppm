export module rstd:async.readiness;
export import rstd.core;

namespace rstd::async
{

export struct Interest {
    u8 m_bits {};

    static constexpr u8 READABLE { 1 };
    static constexpr u8 WRITABLE { 2 };

    static constexpr auto readable() noexcept -> Interest { return Interest { READABLE }; }
    static constexpr auto writable() noexcept -> Interest { return Interest { WRITABLE }; }
    static constexpr auto read_write() noexcept -> Interest {
        return Interest { u8(READABLE | WRITABLE) };
    }

    constexpr auto is_readable() const noexcept -> bool { return (m_bits & READABLE) != 0; }
    constexpr auto is_writable() const noexcept -> bool { return (m_bits & WRITABLE) != 0; }
    constexpr auto is_empty() const noexcept -> bool { return m_bits == 0; }

    friend constexpr auto operator|(Interest a, Interest b) noexcept -> Interest {
        return Interest { u8(a.m_bits | b.m_bits) };
    }
};

export struct Ready {
    u8 m_bits {};

    static constexpr u8 READABLE { 1 };
    static constexpr u8 WRITABLE { 2 };
    static constexpr u8 READ_CLOSED { 4 };
    static constexpr u8 WRITE_CLOSED { 8 };
    static constexpr u8 ERROR { 16 };

    static constexpr auto readable() noexcept -> Ready { return Ready { READABLE }; }
    static constexpr auto writable() noexcept -> Ready { return Ready { WRITABLE }; }
    static constexpr auto read_closed() noexcept -> Ready { return Ready { READ_CLOSED }; }
    static constexpr auto write_closed() noexcept -> Ready { return Ready { WRITE_CLOSED }; }
    static constexpr auto error() noexcept -> Ready { return Ready { ERROR }; }

    constexpr auto is_readable() const noexcept -> bool { return (m_bits & READABLE) != 0; }
    constexpr auto is_writable() const noexcept -> bool { return (m_bits & WRITABLE) != 0; }
    constexpr auto is_read_closed() const noexcept -> bool { return (m_bits & READ_CLOSED) != 0; }
    constexpr auto is_write_closed() const noexcept -> bool { return (m_bits & WRITE_CLOSED) != 0; }
    constexpr auto is_error() const noexcept -> bool { return (m_bits & ERROR) != 0; }
    constexpr auto is_empty() const noexcept -> bool { return m_bits == 0; }

    constexpr auto for_interest(Interest interest) const noexcept -> Ready {
        auto bits = u8(0);
        if (interest.is_readable()) bits |= m_bits & (READABLE | READ_CLOSED | ERROR);
        if (interest.is_writable()) bits |= m_bits & (WRITABLE | WRITE_CLOSED | ERROR);
        return Ready { bits };
    }

    constexpr void remove(Ready ready) noexcept { m_bits &= ~ready.m_bits; }

    friend constexpr auto operator|(Ready a, Ready b) noexcept -> Ready {
        return Ready { u8(a.m_bits | b.m_bits) };
    }

    constexpr auto operator|=(Ready other) noexcept -> Ready& {
        m_bits |= other.m_bits;
        return *this;
    }
};

} // namespace rstd::async
