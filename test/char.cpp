import rstd;
#include <gtest/gtest.h>

using namespace rstd::char_;

TEST(Char, LenUtf8) {
    EXPECT_EQ(len_utf8('A'), 1u);           // ASCII
    EXPECT_EQ(len_utf8(0x00E9), 2u);        // é (U+00E9)
    EXPECT_EQ(len_utf8(0x4E2D), 3u);        // 中 (U+4E2D)
    EXPECT_EQ(len_utf8(0x1F600), 4u);       // 😀 (U+1F600)
    EXPECT_EQ(len_utf8(0xD800), 0u);        // surrogate (invalid)
    EXPECT_EQ(len_utf8(0x110000), 0u);      // out of range
}

TEST(Char, EncodeDecodeAscii) {
    rstd::u8 buf[4];
    EXPECT_EQ(encode_utf8('H', buf), 1u);
    EXPECT_EQ(buf[0], 'H');

    auto [cp, n] = decode_utf8(buf, 1);
    EXPECT_EQ(cp, U'H');
    EXPECT_EQ(n, 1u);
}

TEST(Char, EncodeDecodeTwoByte) {
    rstd::u8 buf[4];
    EXPECT_EQ(encode_utf8(0x00E9, buf), 2u); // é
    auto [cp, n] = decode_utf8(buf, 2);
    EXPECT_EQ(cp, char32_t(0x00E9));
    EXPECT_EQ(n, 2u);
}

TEST(Char, EncodeDecodeThreeByte) {
    rstd::u8 buf[4];
    EXPECT_EQ(encode_utf8(0x4E2D, buf), 3u); // 中
    auto [cp, n] = decode_utf8(buf, 3);
    EXPECT_EQ(cp, char32_t(0x4E2D));
    EXPECT_EQ(n, 3u);
}

TEST(Char, EncodeDecodeFourByte) {
    rstd::u8 buf[4];
    EXPECT_EQ(encode_utf8(0x1F600, buf), 4u); // 😀
    auto [cp, n] = decode_utf8(buf, 4);
    EXPECT_EQ(cp, char32_t(0x1F600));
    EXPECT_EQ(n, 4u);
}

TEST(Char, DecodeInvalidLeading) {
    rstd::u8 bad[] = { 0xFF };
    auto [cp, n] = decode_utf8(bad, 1);
    EXPECT_EQ(cp, REPLACEMENT);
    EXPECT_EQ(n, 1u);
}

TEST(Char, DecodeIncomplete) {
    rstd::u8 bad[] = { 0xC3 }; // start of 2-byte, but only 1 byte
    auto [cp, n] = decode_utf8(bad, 1);
    EXPECT_EQ(cp, REPLACEMENT);
}

TEST(Char, DecodeOverlong) {
    // Overlong encoding of '/' (U+002F): C0 AF
    rstd::u8 bad[] = { 0xC0, 0xAF };
    auto [cp, n] = decode_utf8(bad, 2);
    EXPECT_EQ(cp, REPLACEMENT);
}

TEST(Char, IsContinuation) {
    EXPECT_FALSE(is_continuation(0x41)); // 'A'
    EXPECT_TRUE(is_continuation(0x80));  // continuation byte
    EXPECT_TRUE(is_continuation(0xBF));  // continuation byte
    EXPECT_FALSE(is_continuation(0xC0)); // leading byte
}

TEST(Char, IsCharBoundary) {
    // "é" = C3 A9
    rstd::u8 data[] = { 0xC3, 0xA9, 0x21 }; // "é!"
    EXPECT_TRUE(is_char_boundary(data, 3, 0));
    EXPECT_FALSE(is_char_boundary(data, 3, 1)); // middle of é
    EXPECT_TRUE(is_char_boundary(data, 3, 2));  // start of '!'
    EXPECT_TRUE(is_char_boundary(data, 3, 3));  // end
}

TEST(Char, IsValidUtf8) {
    // Valid: "Hello"
    rstd::u8 good[] = { 'H', 'e', 'l', 'l', 'o' };
    EXPECT_TRUE(is_valid_utf8(good, 5));

    // Valid: "中文" = E4 B8 AD E6 96 87
    rstd::u8 chinese[] = { 0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87 };
    EXPECT_TRUE(is_valid_utf8(chinese, 6));

    // Invalid: lone continuation byte
    rstd::u8 bad[] = { 0x80 };
    EXPECT_FALSE(is_valid_utf8(bad, 1));

    // Invalid: truncated sequence
    rstd::u8 trunc[] = { 0xC3 };
    EXPECT_FALSE(is_valid_utf8(trunc, 1));
}
