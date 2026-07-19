export module rstd:net.socket_addr;
import rstd.core;

namespace rstd::net
{

export class Ipv4Addr {
    u8 m_octets[4] {};

public:
    static constexpr auto make(u8 a, u8 b, u8 c, u8 d) noexcept -> Ipv4Addr {
        auto out        = Ipv4Addr {};
        out.m_octets[0] = a;
        out.m_octets[1] = b;
        out.m_octets[2] = c;
        out.m_octets[3] = d;
        return out;
    }

    static constexpr auto loopback() noexcept -> Ipv4Addr {
        return make(u8(127), u8(), u8(), u8(1));
    }
    static constexpr auto any() noexcept -> Ipv4Addr { return make(u8(), u8(), u8(), u8()); }

    constexpr auto octet(usize index) const noexcept -> u8 {
        return m_octets[index.to_primitive()];
    }

    friend constexpr auto operator==(Ipv4Addr const&, Ipv4Addr const&) noexcept -> bool = default;
};

export class Ipv6Addr {
    u8 m_octets[16] {};

public:
    static constexpr auto make(u16 a, u16 b, u16 c, u16 d, u16 e, u16 f, u16 g, u16 h) noexcept
        -> Ipv6Addr {
        auto out      = Ipv6Addr {};
        u16  parts[8] = { a, b, c, d, e, f, g, h };
        for (rstd::size_t i = 0; i < 8; ++i) {
            auto part               = parts[i].to_primitive();
            out.m_octets[i * 2]     = u8(part >> 8);
            out.m_octets[i * 2 + 1] = u8(part);
        }
        return out;
    }

    static constexpr auto loopback() noexcept -> Ipv6Addr {
        return make(u16(), u16(), u16(), u16(), u16(), u16(), u16(), u16(1));
    }
    static constexpr auto any() noexcept -> Ipv6Addr {
        return make(u16(), u16(), u16(), u16(), u16(), u16(), u16(), u16());
    }

    constexpr auto octet(usize index) const noexcept -> u8 {
        return m_octets[index.to_primitive()];
    }

    friend constexpr auto operator==(Ipv6Addr const&, Ipv6Addr const&) noexcept -> bool = default;
};

export class SocketAddr {
    enum class Kind : rstd::uint8_t
    {
        V4,
        V6
    };

    Kind m_kind { Kind::V4 };
    u8   m_octets[16] {};
    u16  m_port {};
    u32  m_flowinfo {};
    u32  m_scope_id {};

public:
    static constexpr auto ipv4(Ipv4Addr ip, u16 port) noexcept -> SocketAddr {
        auto out   = SocketAddr {};
        out.m_kind = Kind::V4;
        out.m_port = port;
        for (rstd::size_t i = 0; i < 4; ++i) out.m_octets[i] = ip.octet(usize(i));
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
        for (rstd::size_t i = 0; i < 16; ++i) out.m_octets[i] = ip.octet(usize(i));
        return out;
    }

    constexpr auto is_ipv4() const noexcept -> bool { return m_kind == Kind::V4; }
    constexpr auto is_ipv6() const noexcept -> bool { return m_kind == Kind::V6; }
    constexpr auto octet(usize index) const noexcept -> u8 {
        return m_octets[index.to_primitive()];
    }
    constexpr auto port() const noexcept -> u16 { return m_port; }
    constexpr auto flowinfo() const noexcept -> u32 { return m_flowinfo; }
    constexpr auto scope_id() const noexcept -> u32 { return m_scope_id; }

    friend constexpr auto operator==(SocketAddr const&, SocketAddr const&) noexcept
        -> bool = default;
};

} // namespace rstd::net
