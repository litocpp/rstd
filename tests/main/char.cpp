#include <rstd/test/gtest.hpp>
import rstd;
import rstd.tests.char_core_check;
import rstd.tests.char_alloc_check;

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

TEST(Char, UnicodeCategories) {
    static_assert(UNICODE_VERSION[0] == 18 && UNICODE_VERSION[1] == 0 && UNICODE_VERSION[2] == 0);
    static_assert(is_alphabetic(U'中'));
    static_assert(is_alphabetic(U'é'));
    static_assert(is_alphabetic(U'\u0345'));
    static_assert(is_alphabetic(U'\u2167'));
    static_assert(is_alphabetic(U'\u0558'));
    static_assert(! is_alphabetic(U'\u0301'));
    static_assert(is_numeric(U'\u2167'));
    static_assert(is_numeric(U'\u00b2'));
    static_assert(is_numeric(U'\u0663'));
    static_assert(is_numeric(U'\U0001246f'));
    static_assert(! is_numeric(U'三'));
    static_assert(! is_numeric(U'A'));
    static_assert(is_alphanumeric(U'中'));
    static_assert(is_alphanumeric(U'\u00b2'));
    static_assert(! is_alphanumeric(U'💝'));
    static_assert(is_whitespace(U'\u0085'));
    static_assert(is_whitespace(U'\u3000'));
    static_assert(! is_whitespace(U'\u001c'));
    static_assert(! is_whitespace(U'\u200b'));

    for (char32_t c = 0; c < 128; ++c) {
        auto alpha = (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z');
        auto digit = c >= U'0' && c <= U'9';
        EXPECT_EQ(is_alphabetic(c), alpha);
        EXPECT_EQ(is_numeric(c), digit);
        EXPECT_EQ(is_alphanumeric(c), alpha || digit);
        EXPECT_EQ(is_whitespace(c), c == U' ' || (c >= 9 && c <= 13));
    }
    static_assert(! is_alphabetic(0x110000));
    static_assert(! is_numeric(0xD800));
    static_assert(! is_alphanumeric(0xFFFFFFFF));
    static_assert(! is_whitespace(0x110000));
    for (char32_t c : rstd::array<char32_t, 5> { char32_t(0xD800),
                                                 char32_t(0xDFFF),
                                                 char32_t(0x10FFFF),
                                                 char32_t(0x110000),
                                                 char32_t(0xFFFFFFFF) }) {
        EXPECT_FALSE(is_alphabetic(c));
        EXPECT_FALSE(is_numeric(c));
        EXPECT_FALSE(is_alphanumeric(c));
        EXPECT_FALSE(is_whitespace(c));
    }
}

TEST(Char, ScalarConstruction) {
    using rstd::Char;
    using rstd::u32;
    static_assert(sizeof(Char) == 4);
    static_assert(rstd::mtp::triv_copy<Char>);
    static_assert(! __is_constructible(Char, char32_t));
    static_assert(! __is_convertible(Char, char32_t));
    static_assert(! __is_convertible(Char, u32));
    static_assert(Char().to_primitive() == U'\0');
    static_assert(Char::MIN.to_u32() == u32());
    static_assert(Char::MAX.to_primitive() == 0x10FFFF);
    static_assert(Char::REPLACEMENT_CHARACTER.to_primitive() == 0xFFFD);
    static_assert(Char::UNICODE_VERSION[0] == 18);
    static_assert(Char::from_char32(U'中').unwrap().is_alphabetic());
    static_assert(Char::from_u32(u32(0xD800)).is_none());
    static_assert(Char::from_char32(0x110000).is_none());
    constexpr char32_t valid[] { 0,      0x7F,   0x80,    0x7FF,    0x800, 0xD7FF, 0xE000,
                                 0xFFFD, 0xFFFF, 0x10000, 0x10FFFF, 0x378, 0xFDD0 };
    for (auto cp : valid) {
        auto value = Char::from_char32(cp);
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ(value->to_primitive(), cp);
        EXPECT_EQ(Char::from_u32(u32(cp)).unwrap(), *value);
        auto moved = rstd::move(*value);
        EXPECT_EQ(moved, *value);
        EXPECT_EQ(rstd::as<rstd::clone::Clone>(moved).clone(), moved);
    }
    constexpr char32_t invalid[] { 0xD800, 0xDFFF, 0x110000, 0xFFFFFFFF };
    for (auto cp : invalid) {
        EXPECT_TRUE(Char::from_char32(cp).is_none());
        EXPECT_TRUE(Char::from_u32(u32(cp)).is_none());
    }
    EXPECT_LT(Char::MIN, Char::MAX);
    EXPECT_LT(Char::from_char32(0xD7FF).unwrap(), Char::from_char32(0xE000).unwrap());
}

TEST(Char, ScalarUtf8RoundTrip) {
    constexpr char32_t points[] { 0,      0x7F,   0x80,   0x7FF,   0x800,
                                  0xD7FF, 0xE000, 0xFFFF, 0x10000, 0x10FFFF };
    for (auto cp : points) {
        auto       value = rstd::Char::from_char32(cp).unwrap();
        rstd::byte buffer[4] {
            rstd::byte(0xAA), rstd::byte(0xAA), rstd::byte(0xAA), rstd::byte(0xAA)
        };
        auto length = value.encode_utf8(buffer);
        EXPECT_EQ(length, value.len_utf8());
        auto expected = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        EXPECT_EQ(length.to_primitive(), rstd::size_t(expected));
        for (auto i = length.to_primitive(); i < 4; ++i) EXPECT_EQ(buffer[i], rstd::byte(0xAA));
        auto [decoded, consumed] = decode_char_utf8(buffer, length);
        EXPECT_EQ(decoded, value);
        EXPECT_EQ(consumed, length);
        auto [raw, raw_length] = decode_utf8(buffer, length);
        EXPECT_EQ(raw, cp);
        EXPECT_EQ(raw_length, length);
    }
}

TEST(Char, TypedDecodePreservesReplacementConsumption) {
    using rstd::Char;
    auto [empty, zero] = decode_char_utf8(nullptr, rstd::usize());
    EXPECT_EQ(empty, Char::REPLACEMENT_CHARACTER);
    EXPECT_EQ(zero, rstd::usize());
    constexpr rstd::uint8_t cases[][4] { { 0xFF, 0, 0, 0 },       { 0xC0, 0xAF, 0, 0 },
                                         { 0xED, 0xA0, 0x80, 0 }, { 0xF4, 0x90, 0x80, 0x80 },
                                         { 0xE2, 0x82, 0, 0 },    { 0xEF, 0xBF, 0xBD, 0 } };
    constexpr unsigned      lengths[] { 1, 2, 3, 4, 2, 3 };
    for (unsigned i = 0; i < 6; ++i) {
        rstd::byte buffer[4] {};
        for (unsigned j = 0; j < 4; ++j) buffer[j] = rstd::byte(cases[i][j]);
        auto [value, consumed] = decode_char_utf8(buffer, rstd::usize(lengths[i]));
        EXPECT_EQ(value, Char::REPLACEMENT_CHARACTER);
        EXPECT_EQ(consumed, rstd::usize(i == 5 ? 3 : 1));
        EXPECT_EQ(is_valid_utf8(buffer, rstd::usize(lengths[i])), i == 5);
    }
}

TEST(Char, ScalarClassificationAndTraits) {
    auto letter = rstd::Char::from_char32(U'中').unwrap();
    auto number = rstd::Char::from_char32(U'²').unwrap();
    auto space  = rstd::Char::from_char32(U'\u3000').unwrap();
    EXPECT_FALSE(letter.is_ascii());
    EXPECT_TRUE(letter.is_alphabetic());
    EXPECT_TRUE(letter.is_alphanumeric());
    EXPECT_TRUE(number.is_numeric());
    EXPECT_TRUE(space.is_whitespace());
    EXPECT_TRUE(rstd::Char::MIN.is_ascii());
    rstd::hash::DefaultHasher first, second, integer;
    rstd::as<rstd::hash::Hash>(letter).hash(first);
    auto code = letter.to_u32();
    auto copy = rstd::Char::from_u32(code).unwrap();
    rstd::as<rstd::hash::Hash>(copy).hash(second);
    rstd::as<rstd::hash::Hash>(code).hash(integer);
    EXPECT_EQ(first.finish(), second.finish());
    EXPECT_EQ(first.finish(), integer.finish());
    EXPECT_EQ(rstd::format("{}", letter), "中"_str);
    EXPECT_EQ(rstd::format("{:>3}", letter), "  中"_str);
    EXPECT_EQ(rstd::format("{}", rstd::Char::MIN), "\0"_str);
}

TEST(Char, CoreAndAllocImports) {
    EXPECT_TRUE(char_core_check());
    EXPECT_TRUE(char_alloc_check());
}

TEST(Char, LetterCategory) {
    for (auto cp : rstd::array<char32_t, 6> { U'A', U'z', U'中', U'é', U'ǅ', U'ʰ' }) {
        EXPECT_TRUE(rstd::char_::is_letter(cp));
        EXPECT_TRUE(rstd::Char::from_char32(cp).unwrap().is_letter());
    }
    for (auto cp :
         rstd::array<char32_t, 7> { U'0', U'²', U'Ⅷ', U'\u0345', U'\u05b0', U' ', U'😀' }) {
        EXPECT_FALSE(rstd::char_::is_letter(cp));
        EXPECT_FALSE(rstd::Char::from_char32(cp).unwrap().is_letter());
    }
    EXPECT_TRUE(rstd::char_::is_alphabetic(U'\u0345'));
    EXPECT_TRUE(rstd::char_::is_alphabetic(U'Ⅷ'));
    EXPECT_TRUE(rstd::char_::is_numeric(U'Ⅷ'));
}
