#include <gtest/gtest.h>
import rstd;

using namespace rstd::char_;
using namespace rstd::literals;

TEST(Char, LenUtf8) {
    EXPECT_EQ(len_utf8('A'), 1_usize);      // ASCII
    EXPECT_EQ(len_utf8(0x00E9), 2_usize);   // é (U+00E9)
    EXPECT_EQ(len_utf8(0x4E2D), 3_usize);   // 中 (U+4E2D)
    EXPECT_EQ(len_utf8(0x1F600), 4_usize);  // 😀 (U+1F600)
    EXPECT_EQ(len_utf8(0xD800), 0_usize);   // surrogate (invalid)
    EXPECT_EQ(len_utf8(0x110000), 0_usize); // out of range
}

TEST(Char, EncodeDecodeAscii) {
    auto buf = rstd::array<rstd::u8, 4> {};
    EXPECT_EQ(encode_utf8('H', buf.data()), 1_usize);
    EXPECT_EQ(buf[rstd::usize()], rstd::u8('H'));

    auto [cp, n] = decode_utf8(buf.data(), 1_usize);
    EXPECT_EQ(cp, U'H');
    EXPECT_EQ(n, 1_usize);
}

TEST(Char, EncodeDecodeTwoByte) {
    auto buf = rstd::array<rstd::u8, 4> {};
    EXPECT_EQ(encode_utf8(0x00E9, buf.data()), 2_usize); // é
    auto [cp, n] = decode_utf8(buf.data(), 2_usize);
    EXPECT_EQ(cp, char32_t(0x00E9));
    EXPECT_EQ(n, 2_usize);
}

TEST(Char, EncodeDecodeThreeByte) {
    auto buf = rstd::array<rstd::u8, 4> {};
    EXPECT_EQ(encode_utf8(0x4E2D, buf.data()), 3_usize); // 中
    auto [cp, n] = decode_utf8(buf.data(), 3_usize);
    EXPECT_EQ(cp, char32_t(0x4E2D));
    EXPECT_EQ(n, 3_usize);
}

TEST(Char, EncodeDecodeFourByte) {
    auto buf = rstd::array<rstd::u8, 4> {};
    EXPECT_EQ(encode_utf8(0x1F600, buf.data()), 4_usize); // 😀
    auto [cp, n] = decode_utf8(buf.data(), 4_usize);
    EXPECT_EQ(cp, char32_t(0x1F600));
    EXPECT_EQ(n, 4_usize);
}

TEST(Char, DecodeInvalidLeading) {
    auto bad     = rstd::array<rstd::u8, 1> { 0xFF_u8 };
    auto [cp, n] = decode_utf8(bad.data(), 1_usize);
    EXPECT_EQ(cp, REPLACEMENT);
    EXPECT_EQ(n, 1_usize);
}

TEST(Char, DecodeIncomplete) {
    auto bad     = rstd::array<rstd::u8, 1> { 0xC3_u8 }; // start of 2-byte, but only 1 byte
    auto [cp, n] = decode_utf8(bad.data(), 1_usize);
    EXPECT_EQ(cp, REPLACEMENT);
}

TEST(Char, DecodeOverlong) {
    // Overlong encoding of '/' (U+002F): C0 AF
    auto bad     = rstd::array<rstd::u8, 2> { 0xC0_u8, 0xAF_u8 };
    auto [cp, n] = decode_utf8(bad.data(), 2_usize);
    EXPECT_EQ(cp, REPLACEMENT);
}

TEST(Char, IsContinuation) {
    EXPECT_FALSE(is_continuation(0x41_u8)); // 'A'
    EXPECT_TRUE(is_continuation(0x80_u8));  // continuation byte
    EXPECT_TRUE(is_continuation(0xBF_u8));  // continuation byte
    EXPECT_FALSE(is_continuation(0xC0_u8)); // leading byte
}

TEST(Char, IsCharBoundary) {
    // "é" = C3 A9
    auto data = rstd::array<rstd::u8, 3> { 0xC3_u8, 0xA9_u8, 0x21_u8 }; // "é!"
    EXPECT_TRUE(is_char_boundary(data.data(), 3_usize, 0_usize));
    EXPECT_FALSE(is_char_boundary(data.data(), 3_usize, 1_usize)); // middle of é
    EXPECT_TRUE(is_char_boundary(data.data(), 3_usize, 2_usize));  // start of '!'
    EXPECT_TRUE(is_char_boundary(data.data(), 3_usize, 3_usize));  // end
}

TEST(Char, IsValidUtf8) {
    // Valid: "Hello"
    auto good = rstd::array<rstd::u8, 5> {
        rstd::u8('H'), rstd::u8('e'), rstd::u8('l'), rstd::u8('l'), rstd::u8('o')
    };
    EXPECT_TRUE(is_valid_utf8(good.data(), 5_usize));

    // Valid: "中文" = E4 B8 AD E6 96 87
    auto chinese =
        rstd::array<rstd::u8, 6> { 0xE4_u8, 0xB8_u8, 0xAD_u8, 0xE6_u8, 0x96_u8, 0x87_u8 };
    EXPECT_TRUE(is_valid_utf8(chinese.data(), 6_usize));

    // Invalid: lone continuation byte
    auto bad = rstd::array<rstd::u8, 1> { 0x80_u8 };
    EXPECT_FALSE(is_valid_utf8(bad.data(), 1_usize));

    // Invalid: truncated sequence
    auto trunc = rstd::array<rstd::u8, 1> { 0xC3_u8 };
    EXPECT_FALSE(is_valid_utf8(trunc.data(), 1_usize));
}
