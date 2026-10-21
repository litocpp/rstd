export module rstd.net.datagram;
export import rstd.core;
export import rstd.net.socket_addr;

namespace rstd::net
{
export struct PacketInfo {
    SocketAddr local;
    u32        interface_index {};
};

export struct DatagramInfo {
    SocketAddr         peer;
    Option<PacketInfo> packet;
    bool               truncated {};
    bool               control_truncated {};
};

export struct DatagramTarget {
    SocketAddr         peer;
    Option<PacketInfo> source;
};

export struct DatagramCapabilities {
    bool destination_address {};
    bool interface_index {};
    bool source_selection {};
};
} // namespace rstd::net
