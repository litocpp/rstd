#include <rstd/test/gtest.hpp>
import rstd.net.lookup_host;
import rstd.test;
import rstd.alloc;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::net::lookup_host;
using rstd::net::Ipv4Addr;
using rstd::net::Ipv6Addr;
using rstd::net::SocketAddr;

TEST(LookupHost, NumericAddressesPreserveFamilyAndPort) {
    auto v4 = lookup_host("192.0.2.1"_str, u16(65535)).unwrap();
    ASSERT_EQ(v4.len(), usize(1));
    EXPECT_EQ(v4[usize()],
              SocketAddr::ipv4(Ipv4Addr::make(u8(192), u8(), u8(2), u8(1)), u16(65535)));
    auto v6 = lookup_host("2001:db8::1"_str, u16()).unwrap();
    ASSERT_EQ(v6.len(), usize(1));
    EXPECT_EQ(v6[usize()], SocketAddr::ipv6(Ipv6Addr::parse("2001:db8::1"_str).unwrap(), u16()));
    auto mapped = lookup_host("::ffff:192.0.2.1"_str, u16(80)).unwrap();
    ASSERT_EQ(mapped.len(), usize(1));
    EXPECT_EQ(mapped[usize()],
              SocketAddr::ipv6(Ipv6Addr::parse("::ffff:192.0.2.1"_str).unwrap(), u16(80)));
}

TEST(LookupHost, RejectsEmptyAndInteriorNul) {
    const ref<str> hosts[] {
        ""_str, "localhost\0suffix"_str, "127.0.0.1\0suffix"_str, "::1\0suffix"_str
    };
    for (auto host : hosts) {
        auto result = lookup_host(host, u16(80));
        ASSERT_TRUE(result.is_err());
        auto error = result.unwrap_err();
        EXPECT_EQ(error.code, i32());
        ASSERT_TRUE(error.cause.is_some());
        EXPECT_EQ(error.cause->kind(),
                  rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::InvalidInput });
        EXPECT_TRUE(rstd::as<rstd::error::Error>(error).source().is_some());
        auto copy = error.clone();
        EXPECT_EQ(rstd::format("{}", copy), rstd::format("{:?}", error));
    }
}

TEST(LookupHost, LocalhostUsesSystemResolverAndReturnsOwnedDistinctAddresses) {
    auto result = lookup_host("localhost"_str, u16(12345)).unwrap();
    ASSERT_FALSE(result.is_empty());
    for (usize i; i < result.len(); ++i) {
        EXPECT_EQ(result[i].port(), u16(12345));
        EXPECT_TRUE(result[i].is_ipv4() || result[i].is_ipv6());
        for (usize j; j < i; ++j) EXPECT_FALSE(result[i] == result[j]);
    }
    auto next = lookup_host("localhost"_str, u16(54321)).unwrap();
    ASSERT_FALSE(next.is_empty());
    EXPECT_EQ(result[usize()].port(), u16(12345));
    EXPECT_EQ(next[usize()].port(), u16(54321));
}

auto main() -> int {
    return rstd::test::run_registered().to_primitive();
}
