module;
#include <rstd/test/gtest.hpp>
export module rstd:ffi.os_encoding_tests;
import :ffi.os_str.encoding;
import :sys.args.windows;
import rstd.alloc;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::vec::Vec;

TEST(OsEncoding, Utf16SurrogatesAndConcatenation) {
    const u16 units[] = { u16(0xd800), u16('x'), u16(0xdc00), u16(0xd83d), u16(0xde00) };
    auto bytes = rstd::ffi::os_encoding::from_wide(slice<u16>::from_raw_parts(units, usize(5)));
    EXPECT_EQ(bytes.as_slice(), "\xed\xa0\x80x\xed\xb0\x80\xf0\x9f\x98\x80"_bytes);
    auto left = Vec<u8>::from("\xed\xa0\x80"_bytes);
    rstd::ffi::os_encoding::append_wtf8(left, "\xed\xb0\x80!"_bytes);
    EXPECT_EQ(left.as_slice(), "\xf0\x90\x80\x80!"_bytes);
    auto offset = usize();
    EXPECT_EQ(rstd::ffi::os_encoding::decode(left.as_slice(), offset), 0x10000u);
    EXPECT_EQ(offset, usize(4));
}

TEST(OsEncoding, PublicBoundaryProtectsUtf8) {
    EXPECT_TRUE(rstd::ffi::os_encoding::public_boundary("é!"_bytes, usize(2)));
    EXPECT_FALSE(rstd::ffi::os_encoding::public_boundary("é!"_bytes, usize(1)));
    EXPECT_TRUE(rstd::ffi::os_encoding::public_boundary("\xff!"_bytes, usize(1)));
    EXPECT_FALSE(rstd::ffi::os_encoding::public_boundary("\xff\xfe"_bytes, usize(1)));
    EXPECT_FALSE(rstd::ffi::os_encoding::public_boundary("abc"_bytes, usize(4)));
}

TEST(OsEncoding, WindowsArgumentRules) {
    auto wide = Vec<u16>::make();
    for (auto byte : "\"my program\" \"\" \"a\"\"b\" tail\\"_bytes)
        wide.push(u16(byte.to_primitive()));
    auto parsed = rstd::sys::args::windows::parse(wide.as_slice());
    ASSERT_EQ(parsed.len(), usize(4));
    EXPECT_EQ(parsed[usize()].as_slice(), "my program"_bytes);
    EXPECT_TRUE(parsed[usize(1)].is_empty());
    EXPECT_EQ(parsed[usize(2)].as_slice(), "a\"b"_bytes);
    EXPECT_EQ(parsed[usize(3)].as_slice(), "tail\\"_bytes);
}
