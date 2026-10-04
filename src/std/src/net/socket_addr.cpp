module rstd;
import :net.socket_addr;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::net;

auto address_number(ref<str> text, u32 radix, u32 maximum) -> Option<u32> {
    if (text.is_empty()) return None();
    u32 number {};
    for (auto byte : text.as_bytes()) {
        auto     c     = byte.to_primitive();
        unsigned digit = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                : 99;
        if (u32(digit) >= radix || number > (maximum - u32(digit)) / radix) return None();
        number = number * radix + u32(digit);
    }
    return Some(number);
}
auto address_ipv4(ref<str> text) -> Option<array<u8, 4>> {
    array<u8, 4> bytes {};
    for (usize i {}; i < usize(4); ++i) {
        auto split = text.split_once("."_str);
        if ((i < usize(3)) != split.is_some()) return None();
        auto part = split.is_some() ? split->get<0>() : text;
        if (part.len() > usize(1) && part.starts_with("0"_str)) return None();
        auto number = address_number(part, u32(10), u32(255));
        if (number.is_none()) return None();
        bytes[i] = u8(number->to_primitive());
        if (split.is_some()) text = split->get<1>();
    }
    return Some(bytes);
}
auto address_ipv6_part(ref<str> text, array<u16, 8>& groups, usize& count, bool ipv4) -> bool {
    if (text.is_empty()) return true;
    for (;;) {
        auto split = text.split_once(":"_str);
        auto part  = split.is_some() ? split->get<0>() : text;
        if (part.contains("."_str)) {
            if (! ipv4 || split.is_some() || count > usize(6)) return false;
            auto bytes = address_ipv4(part);
            if (bytes.is_none()) return false;
            groups[count++] = u16(u8((*bytes)[usize()]).to_primitive()) * u16(256) +
                              u16(u8((*bytes)[usize(1)]).to_primitive());
            groups[count++] = u16(u8((*bytes)[usize(2)]).to_primitive()) * u16(256) +
                              u16(u8((*bytes)[usize(3)]).to_primitive());
        } else {
            if (part.len() > usize(4) || count >= usize(8)) return false;
            auto number = address_number(part, u32(16), u32(65535));
            if (number.is_none()) return false;
            groups[count++] = u16(number->to_primitive());
        }
        if (split.is_none()) return true;
        text = split->get<1>();
        if (text.is_empty()) return false;
    }
}

auto rstd::net::SocketAddr::parse(ref<str> text) -> Result<SocketAddr, AddrParseError> {
    auto invalid = [] {
        return Err(AddrParseError::Invalid);
    };
    auto separator = text.rsplit_once(":"_str);
    if (separator.is_none()) return invalid();
    auto port = address_number(separator->get<1>(), u32(10), u32(65535));
    if (port.is_none()) return invalid();
    auto host = separator->get<0>();
    if (! host.starts_with("["_str)) {
        auto bytes = address_ipv4(host);
        if (bytes.is_none()) return invalid();
        return Ok(
            ipv4(Ipv4Addr::make(
                     (*bytes)[usize()], (*bytes)[usize(1)], (*bytes)[usize(2)], (*bytes)[usize(3)]),
                 u16(port->to_primitive())));
    }
    if (! host.ends_with("]"_str)) return invalid();
    host = host.strip_prefix("["_str).unwrap().strip_suffix("]"_str).unwrap();
    u32 scope {};
    if (auto split = host.split_once("%"_str)) {
        auto value = address_number(split->get<1>(), u32(10), u32::MAX);
        if (value.is_none()) return invalid();
        scope = *value;
        host  = split->get<0>();
    }
    array<u16, 8> groups {}, tail {};
    usize         count {}, tail_count {};
    auto          compressed = host.split_once("::"_str);
    if (compressed.is_some()) {
        if (! address_ipv6_part(compressed->get<0>(), groups, count, false) ||
            ! address_ipv6_part(compressed->get<1>(), tail, tail_count, true) ||
            count + tail_count >= usize(8))
            return invalid();
        for (usize i {}; i < tail_count; ++i) groups[usize(8) - tail_count + i] = tail[i];
    } else if (! address_ipv6_part(host, groups, count, true) || count != usize(8))
        return invalid();
    return Ok(ipv6(Ipv6Addr::make(groups[usize()],
                                  groups[usize(1)],
                                  groups[usize(2)],
                                  groups[usize(3)],
                                  groups[usize(4)],
                                  groups[usize(5)],
                                  groups[usize(6)],
                                  groups[usize(7)]),
                   u16(port->to_primitive()),
                   u32(),
                   scope));
}
