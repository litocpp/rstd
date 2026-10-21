module rstd.net.socket_addr;

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

auto rstd::net::Ipv4Addr::parse(ref<str> text) -> Result<Ipv4Addr, AddrParseError> {
    auto bytes = address_ipv4(text);
    if (bytes.is_none()) return Err(AddrParseError::InvalidIp);
    return Ok(make((*bytes)[usize()], (*bytes)[usize(1)], (*bytes)[usize(2)], (*bytes)[usize(3)]));
}

auto rstd::net::Ipv6Addr::parse(ref<str> text) -> Result<Ipv6Addr, AddrParseError> {
    array<u16, 8> groups {}, tail {};
    usize         count {}, tail_count {};
    auto          compressed = text.split_once("::"_str);
    if (compressed.is_some()) {
        if (! address_ipv6_part(compressed->get<0>(), groups, count, false) ||
            ! address_ipv6_part(compressed->get<1>(), tail, tail_count, true) ||
            count + tail_count >= usize(8))
            return Err(AddrParseError::InvalidIp);
        for (usize i; i < tail_count; ++i) groups[usize(8) - tail_count + i] = tail[i];
    } else if (! address_ipv6_part(text, groups, count, true) || count != usize(8))
        return Err(AddrParseError::InvalidIp);
    return Ok(make(groups[usize()],
                   groups[usize(1)],
                   groups[usize(2)],
                   groups[usize(3)],
                   groups[usize(4)],
                   groups[usize(5)],
                   groups[usize(6)],
                   groups[usize(7)]));
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
        auto address = Ipv4Addr::parse(host);
        if (address.is_err()) return invalid();
        return Ok(ipv4(*address, u16(port->to_primitive())));
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
    auto address = Ipv6Addr::parse(host);
    if (address.is_err()) return invalid();
    return Ok(ipv6(*address, u16(port->to_primitive()), u32(), scope));
}

auto rstd::net::format_address(fmt::Formatter& f, const SocketAddr& a) -> bool {
    if (a.is_ipv4())
        return f.write_fmt(fmt::Arguments::make("{}.{}.{}.{}:{}",
                                                a.octet(usize()),
                                                a.octet(usize(1)),
                                                a.octet(usize(2)),
                                                a.octet(usize(3)),
                                                a.port()));
    array<u16, 8> groups {};
    for (usize i; i < usize(8); ++i)
        groups[i] = u16(a.octet(i * usize(2)).to_primitive()) * u16(256) +
                    u16(a.octet(i * usize(2) + usize(1)).to_primitive());
    usize best(8), length;
    for (usize i; i < usize(8);) {
        if (groups[i] != u16()) {
            ++i;
            continue;
        }
        auto start = i;
        while (i < usize(8) && groups[i] == u16()) ++i;
        if (i - start > length && i - start >= usize(2)) {
            best   = start;
            length = i - start;
        }
    }
    if (! f.write_str("["_str)) return false;
    for (usize i; i < usize(8);) {
        if (i == best) {
            if (! f.write_str("::"_str)) return false;
            i += length;
            continue;
        }
        if (i != usize() && i != best + length && ! f.write_str(":"_str)) return false;
        if (! f.write_fmt(fmt::Arguments::make("{:x}", u16(groups[i])))) return false;
        ++i;
    }
    if (a.scope_id() != u32() && ! f.write_fmt(fmt::Arguments::make("%{}", a.scope_id())))
        return false;
    return f.write_fmt(fmt::Arguments::make("]:{}", a.port()));
}
