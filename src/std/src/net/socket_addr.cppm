export module rstd.net.socket_addr;
import rstd.core;

namespace rstd::net
{
export enum class AddrParseError { Invalid, InvalidIp };

export class Ipv4Addr {
    array<u8, 4> m_octets {};

public:
    static auto           parse(ref<str> text) -> Result<Ipv4Addr, AddrParseError>;
    static constexpr auto make(u8 a, u8 b, u8 c, u8 d) noexcept -> Ipv4Addr {
        auto out               = Ipv4Addr {};
        out.m_octets[usize()]  = a;
        out.m_octets[usize(1)] = b;
        out.m_octets[usize(2)] = c;
        out.m_octets[usize(3)] = d;
        return out;
    }

    static constexpr auto loopback() noexcept -> Ipv4Addr {
        return make(u8(127), u8(), u8(), u8(1));
    }
    static constexpr auto any() noexcept -> Ipv4Addr { return make(u8(), u8(), u8(), u8()); }

    constexpr auto octet(usize index) const noexcept -> u8 { return m_octets[index]; }

    friend constexpr auto operator==(Ipv4Addr const& left, Ipv4Addr const& right) noexcept -> bool {
        for (rstd::size_t index = 0; index < 4; ++index) {
            if (left.m_octets[usize(index)] != right.m_octets[usize(index)]) return false;
        }
        return true;
    }
};

export class Ipv6Addr {
    array<u8, 16> m_octets {};

public:
    static auto           parse(ref<str> text) -> Result<Ipv6Addr, AddrParseError>;
    static constexpr auto make(u16 a, u16 b, u16 c, u16 d, u16 e, u16 f, u16 g, u16 h) noexcept
        -> Ipv6Addr {
        auto out      = Ipv6Addr {};
        u16  parts[8] = { a, b, c, d, e, f, g, h };
        for (rstd::size_t i = 0; i < 8; ++i) {
            auto part                      = parts[i].to_primitive();
            out.m_octets[usize(i * 2)]     = u8(part >> 8);
            out.m_octets[usize(i * 2 + 1)] = u8(part);
        }
        return out;
    }

    static constexpr auto loopback() noexcept -> Ipv6Addr {
        return make(u16(), u16(), u16(), u16(), u16(), u16(), u16(), u16(1));
    }
    static constexpr auto any() noexcept -> Ipv6Addr {
        return make(u16(), u16(), u16(), u16(), u16(), u16(), u16(), u16());
    }

    constexpr auto octet(usize index) const noexcept -> u8 { return m_octets[index]; }

    friend constexpr auto operator==(Ipv6Addr const& left, Ipv6Addr const& right) noexcept -> bool {
        for (rstd::size_t index = 0; index < 16; ++index) {
            if (left.m_octets[usize(index)] != right.m_octets[usize(index)]) return false;
        }
        return true;
    }
};

export class SocketAddr {
    enum class Kind : rstd::uint8_t
    {
        V4,
        V6
    };

    Kind          m_kind { Kind::V4 };
    array<u8, 16> m_octets {};
    u16           m_port {};
    u32           m_flowinfo {};
    u32           m_scope_id {};

public:
    static auto           parse(ref<str> text) -> Result<SocketAddr, AddrParseError>;
    static constexpr auto ipv4(Ipv4Addr ip, u16 port) noexcept -> SocketAddr {
        auto out   = SocketAddr {};
        out.m_kind = Kind::V4;
        out.m_port = port;
        for (rstd::size_t i = 0; i < 4; ++i) out.m_octets[usize(i)] = ip.octet(usize(i));
        return out;
    }

    static constexpr auto ipv4_loopback(u16 port) noexcept -> SocketAddr {
        return ipv4(Ipv4Addr::loopback(), port);
    }

    static constexpr auto ipv4_any(u16 port) noexcept -> SocketAddr {
        return ipv4(Ipv4Addr::any(), port);
    }

    static constexpr auto
    ipv6(Ipv6Addr ip, u16 port, u32 flowinfo = u32(), u32 scope_id = u32()) noexcept -> SocketAddr {
        auto out       = SocketAddr {};
        out.m_kind     = Kind::V6;
        out.m_port     = port;
        out.m_flowinfo = flowinfo;
        out.m_scope_id = scope_id;
        for (rstd::size_t i = 0; i < 16; ++i) out.m_octets[usize(i)] = ip.octet(usize(i));
        return out;
    }

    constexpr auto is_loopback() const noexcept -> bool {
        if (is_ipv4()) return octet(usize()) == u8(127);
        for (usize i; i < usize(15); ++i)
            if (octet(i) != u8()) return false;
        return octet(usize(15)) == u8(1);
    }
    constexpr auto is_ipv4() const noexcept -> bool { return m_kind == Kind::V4; }
    constexpr auto is_ipv6() const noexcept -> bool { return m_kind == Kind::V6; }
    constexpr auto octet(usize index) const noexcept -> u8 { return m_octets[index]; }
    constexpr auto port() const noexcept -> u16 { return m_port; }
    constexpr auto flowinfo() const noexcept -> u32 { return m_flowinfo; }
    constexpr auto scope_id() const noexcept -> u32 { return m_scope_id; }
    constexpr auto with_port(u16 port) const noexcept -> SocketAddr {
        auto result   = *this;
        result.m_port = port;
        return result;
    }
    constexpr auto ipv4_address() const noexcept -> Option<Ipv4Addr> {
        if (! is_ipv4()) return None();
        return Some(
            Ipv4Addr::make(octet(usize()), octet(usize(1)), octet(usize(2)), octet(usize(3))));
    }
    constexpr auto ipv6_address() const noexcept -> Option<Ipv6Addr> {
        if (! is_ipv6()) return None();
        auto word = [&](unsigned index) {
            return u16((unsigned(octet(usize(index)).to_primitive()) << 8) |
                       octet(usize(index + 1)).to_primitive());
        };
        return Some(Ipv6Addr::make(
            word(0), word(2), word(4), word(6), word(8), word(10), word(12), word(14)));
    }

    friend constexpr auto operator==(SocketAddr const& left, SocketAddr const& right) noexcept
        -> bool {
        if (left.m_kind != right.m_kind || left.m_port != right.m_port ||
            left.m_flowinfo != right.m_flowinfo || left.m_scope_id != right.m_scope_id) {
            return false;
        }
        for (rstd::size_t index = 0; index < 16; ++index) {
            if (left.m_octets[usize(index)] != right.m_octets[usize(index)]) return false;
        }
        return true;
    }
};

export auto format_address(fmt::Formatter& formatter, const SocketAddr& address) -> bool;
export auto format_ip(fmt::Formatter& formatter, const SocketAddr& address) -> bool;

} // namespace rstd::net

namespace rstd
{
template<>
struct Impl<str_::FromStr, net::Ipv4Addr> {
    using Err = net::AddrParseError;
    static auto from_str(ref<str> input) -> Result<net::Ipv4Addr, Err> {
        return net::Ipv4Addr::parse(input);
    }
};

template<>
struct Impl<str_::FromStr, net::Ipv6Addr> {
    using Err = net::AddrParseError;
    static auto from_str(ref<str> input) -> Result<net::Ipv6Addr, Err> {
        return net::Ipv6Addr::parse(input);
    }
};

template<>
struct Impl<str_::FromStr, net::SocketAddr> {
    using Err = net::AddrParseError;
    static auto from_str(ref<str> input) -> Result<net::SocketAddr, Err> {
        return net::SocketAddr::parse(input);
    }
};

template<>
struct Impl<fmt::Display, net::AddrParseError> : ImplBase<net::AddrParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self() == net::AddrParseError::InvalidIp) {
            constexpr char message[] = "invalid IP address syntax";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "invalid socket address syntax";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Debug, net::AddrParseError> : ImplBase<net::AddrParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self() == net::AddrParseError::InvalidIp) {
            constexpr char message[] = "AddrParseError(Ip)";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "AddrParseError(Socket)";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<error::Error, net::AddrParseError> : DefaultInImpl<error::Error, net::AddrParseError> {
};
} // namespace rstd

namespace rstd
{
export template<>
struct Impl<fmt::Display, net::SocketAddr> : ImplBase<net::SocketAddr> {
    auto fmt(fmt::Formatter& f) const -> bool { return net::format_address(f, this->self()); }
};
export template<>
struct Impl<fmt::Debug, net::SocketAddr> : Impl<fmt::Display, net::SocketAddr> {};
export template<>
struct Impl<fmt::Display, net::Ipv4Addr> : ImplBase<net::Ipv4Addr> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return net::format_ip(f, net::SocketAddr::ipv4(this->self(), u16()));
    }
};
export template<>
struct Impl<fmt::Debug, net::Ipv4Addr> : Impl<fmt::Display, net::Ipv4Addr> {};
export template<>
struct Impl<fmt::Display, net::Ipv6Addr> : ImplBase<net::Ipv6Addr> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return net::format_ip(f, net::SocketAddr::ipv6(this->self(), u16()));
    }
};
export template<>
struct Impl<fmt::Debug, net::Ipv6Addr> : Impl<fmt::Display, net::Ipv6Addr> {};
} // namespace rstd
