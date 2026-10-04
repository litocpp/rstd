#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::net::Ipv4Addr;
using rstd::net::Ipv6Addr;
using rstd::net::SocketAddr;

void check_address(ref<str> input, SocketAddr expected) {
    auto direct = SocketAddr::parse(input);
    ASSERT_TRUE(direct.is_ok());
    EXPECT_TRUE(*direct == expected);
    auto generic = rstd::from_str<SocketAddr>(input);
    ASSERT_TRUE(generic.is_ok());
    EXPECT_TRUE(*generic == expected);
}

TEST(SocketAddr, ParsesIpv4AndPortBoundaries) {
    check_address("0.0.0.0:0"_str, SocketAddr::ipv4_any(u16()));
    check_address("127.0.0.1:80"_str, SocketAddr::ipv4_loopback(u16(80)));
    check_address("255.255.255.255:65535"_str,
                  SocketAddr::ipv4(Ipv4Addr::make(u8(255), u8(255), u8(255), u8(255)), u16(65535)));
    check_address("127.0.0.1:00000000000000000080"_str, SocketAddr::ipv4_loopback(u16(80)));
}

TEST(SocketAddr, ParsesIpv6AndNumericScope) {
    check_address("[::]:0"_str, SocketAddr::ipv6(Ipv6Addr::any(), u16()));
    check_address("[::1]:443"_str, SocketAddr::ipv6(Ipv6Addr::loopback(), u16(443)));
    check_address("[::1%00000000000000000012]:00080"_str,
                  SocketAddr::ipv6(Ipv6Addr::loopback(), u16(80), u32(), u32(12)));
    check_address("[::1%4294967295]:65535"_str,
                  SocketAddr::ipv6(Ipv6Addr::loopback(), u16(65535), u32(), u32::MAX));
    check_address(
        "[2001:dB8:0000:0000:0:0:ABCD:Ef01]:90"_str,
        SocketAddr::ipv6(
            Ipv6Addr::make(
                u16(0x2001), u16(0xdb8), u16(), u16(), u16(), u16(), u16(0xabcd), u16(0xef01)),
            u16(90)));
}

TEST(SocketAddr, ExpandsEveryCompressionPosition) {
    for (usize omitted(1); omitted <= usize(8); ++omitted) {
        for (usize first; first + omitted <= usize(8); ++first) {
            String        input = "["_Str;
            array<u16, 8> parts {};
            for (usize i; i < first; ++i) {
                if (i != usize()) input.push_ascii(':');
                parts[i] = u16(i.to_primitive() + 1);
                input.push_str(rstd::format("{:x}", parts[i]).as_str());
            }
            input.push_str("::"_str);
            for (usize i = first + omitted; i < usize(8); ++i) {
                if (i != first + omitted) input.push_ascii(':');
                parts[i] = u16(i.to_primitive() + 1);
                input.push_str(rstd::format("{:x}", parts[i]).as_str());
            }
            input.push_str("]:123"_str);
            check_address(input.as_str(),
                          SocketAddr::ipv6(Ipv6Addr::make(parts[usize()],
                                                          parts[usize(1)],
                                                          parts[usize(2)],
                                                          parts[usize(3)],
                                                          parts[usize(4)],
                                                          parts[usize(5)],
                                                          parts[usize(6)],
                                                          parts[usize(7)]),
                                           u16(123)));
        }
    }
}

TEST(SocketAddr, ParsesEmbeddedIpv4WithoutChangingAddressFamily) {
    auto ip =
        Ipv6Addr::make(u16(), u16(), u16(), u16(), u16(), u16(0xffff), u16(0xc000), u16(0x280));
    check_address("[::ffff:192.0.2.128]:80"_str, SocketAddr::ipv6(ip, u16(80)));
    check_address("[0:0:0:0:0:ffff:192.0.2.128%3]:80"_str,
                  SocketAddr::ipv6(ip, u16(80), u32(), u32(3)));
    check_address(
        "[::192.0.2.128]:80"_str,
        SocketAddr::ipv6(
            Ipv6Addr::make(u16(), u16(), u16(), u16(), u16(), u16(), u16(0xc000), u16(0x280)),
            u16(80)));
}

TEST(SocketAddr, RejectsNonNumericAndIncompleteAddresses) {
    const ref<str> inputs[] { ""_str,
                              "localhost:80"_str,
                              "example.com:80"_str,
                              ":80"_str,
                              "127.0.0.1"_str,
                              "127.0.0.1:"_str,
                              "127.1:80"_str,
                              "127.0.0.1.2:80"_str,
                              "256.0.0.1:80"_str,
                              "01.0.0.1:80"_str,
                              "0.00.0.1:80"_str,
                              "+127.0.0.1:80"_str,
                              "0x7f.0.0.1:80"_str,
                              "127..0.1:80"_str,
                              "127.0.0.1:-1"_str,
                              "127.0.0.1:+80"_str,
                              "127.0.0.1:65536"_str,
                              "127.0.0.1:4294967296"_str,
                              "127.0.0.1:0x50"_str,
                              " 127.0.0.1:80"_str,
                              "127.0.0.1:80 "_str,
                              "127.0.0.1:80\n"_str,
                              "127.0.0.1:80\0suffix"_str,
                              "１２７.0.0.1:80"_str,
                              "127.0.0.1:８０"_str,
                              "[127.0.0.1]:80"_str,
                              "127.0.0.1%1:80"_str,
                              "::1:80"_str,
                              "[::1]"_str,
                              "[::1]:65536"_str,
                              "[::1]:80suffix"_str,
                              "[]:80"_str,
                              "[:]:80"_str,
                              "[:::]:80"_str,
                              "[1:]:80"_str,
                              "[:1]:80"_str,
                              "[1::2::3]:80"_str,
                              "[1:2:3:4:5:6:7]:80"_str,
                              "[1:2:3:4:5:6:7:8:9]:80"_str,
                              "[1:2:3:4:5:6:7:8::]:80"_str,
                              "[::1:2:3:4:5:6:7:8]:80"_str,
                              "[00000::]:80"_str,
                              "[0x1::]:80"_str,
                              "[gggg::]:80"_str,
                              "[+1::]:80"_str,
                              "[::ffff:192.00.2.1]:80"_str,
                              "[1:2:3:4:5:6:7:192.0.2.1]:80"_str,
                              "[1:2:3:4:5::192.0.2.1:2]:80"_str,
                              "[1:2:3:4:5:6::192.0.2.1]:80"_str,
                              "[192.0.2.1::]:80"_str,
                              "[::1%eth0]:80"_str,
                              "[::1%]:80"_str,
                              "[::1%-1]:80"_str,
                              "[::1%+1]:80"_str,
                              "[::1%4294967296]:80"_str,
                              "[::1%1%2]:80"_str,
                              "[::1]%1:80"_str,
                              "[::1%1]:80:90"_str,
                              "[::1\0]:80"_str };
    for (auto input : inputs) {
        EXPECT_TRUE(SocketAddr::parse(input).is_err());
        EXPECT_TRUE(rstd::from_str<SocketAddr>(input).is_err());
    }
}

TEST(SocketAddr, ParseErrorImplementsFormattingAndError) {
    auto error = SocketAddr::parse("invalid"_str).unwrap_err();
    EXPECT_EQ(rstd::format("{}", error), "invalid socket address syntax"_str);
    EXPECT_EQ(rstd::format("{:?}", error), "AddrParseError(Socket)"_str);
    EXPECT_TRUE(rstd::as<rstd::error::Error>(error).source().is_none());
}

auto main() -> int {
    return rstd::test::run_registered().to_primitive();
}
