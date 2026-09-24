#include <rstd/test/gtest.hpp>
import rstd;
import rstd.core;

using namespace rstd;
using namespace rstd::literals;
using rstd::prelude::String;

static_assert(Impled<f32, fmt::Display>);
static_assert(Impled<f32, fmt::Debug>);
static_assert(Impled<f64, fmt::LowerExp>);
static_assert(Impled<f64, fmt::UpperExp>);
static_assert(Impled<double, fmt::Display>);
static_assert(Impled<double, fmt::Debug>);
static_assert(Impled<double, fmt::LowerExp>);
static_assert(Impled<double, fmt::UpperExp>);

TEST(Fmt, Basic) {
    auto s = rstd::format("Hello, {}!", "world"_str);
    EXPECT_EQ(s, "Hello, world!"_str);
}

TEST(Fmt, MultipleArgs) {
    auto s = rstd::format("{} + {} = {}", 1, 2, 3);
    EXPECT_EQ(s, "1 + 2 = 3"_str);
}

TEST(Fmt, Escaping) {
    auto s = rstd::format("{{ Hello {} }}", "world"_str);
    EXPECT_EQ(s, "{ Hello world }"_str);
}

TEST(Fmt, StringDisplayDebugWidthAndPrecision) {
    auto value = String::make("hello"_str);
    static_assert(Impled<String, fmt::Display>);
    static_assert(Impled<String, fmt::Debug>);

    EXPECT_EQ(rstd::format("{}", value), "hello"_str);
    EXPECT_EQ(rstd::format("{:?}", value), "\"hello\""_str);
    EXPECT_EQ(rstd::format("{:.2?}", value), "\"hello\""_str);
    EXPECT_EQ(rstd::format("{:>7.3}", value), "    hel"_str);

    auto unicode = String::make("é中x"_str);
    EXPECT_EQ(rstd::format("{:4.2}", unicode), "é中  "_str);
    EXPECT_EQ(rstd::format("{:_^5.2}", unicode), "_é中__"_str);
}

TEST(Fmt, IntegerTypes) {
    auto s = rstd::format("Values: {}, {}, {}, {}", (i32)-42, (u64)123456789, (int)0, (i8)-1);
    EXPECT_EQ(s, "Values: -42, 123456789, 0, -1"_str);
}

TEST(Fmt, IntegerHex) {
    EXPECT_EQ(rstd::format("{:x} {:X} {:#x} {:#X}", u32(48879), u32(48879), u32(48879), u32(48879)),
              "beef BEEF 0xbeef 0xBEEF"_str);
    EXPECT_EQ(rstd::format("{:x} {:#X}", u8(), u64()), "0 0x0"_str);
    EXPECT_EQ(rstd::format("{:x} {:X} {:x}", 255, 48879u, usize(42)), "ff BEEF 2a"_str);
    EXPECT_EQ(rstd::format("{:x} {:x} {:x} {:x}", i8(-1), i16(-1), i32(-1), i64(-1)),
              "ff ffff ffffffff ffffffffffffffff"_str);
    EXPECT_EQ(rstd::format("{:x} {:x} {:x}", i8::MIN, i64::MIN, i128::MIN),
              "80 8000000000000000 80000000000000000000000000000000"_str);
    EXPECT_EQ(rstd::format("{:x} {:X}", u128::MAX, i128(-1)),
              "ffffffffffffffffffffffffffffffff FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"_str);
    EXPECT_EQ(rstd::format("{:x}", static_cast<rstd::int16_t>(-2)), "fffe"_str);
}

TEST(Fmt, IntegerHexPadding) {
    EXPECT_EQ(rstd::format("{:08x} {:#08x} {:+#08X}", u32(42), u32(42), u32(42)),
              "0000002a 0x00002a +0x0002A"_str);
    EXPECT_EQ(rstd::format("{:6x}|{:<6X}|{:_^7x}", u32(42), u32(42), u32(42)),
              "    2a|2A    |__2a___"_str);
    EXPECT_EQ(rstd::format("{:0>8x}|{:0>#8x}|{:<#08x}", u32(42), u32(42), u32(42)),
              "0000002a|00000x2a|0x00002a"_str);
    EXPECT_EQ(rstd::format("{:#02x} {:+#06x} {:.4x}", u32(255), u8(), u32(42)),
              "0xff +0x000 2a"_str);
    EXPECT_EQ(rstd::format("{:+#08x}", i8(-1)), "+0x000ff"_str);
}

struct HexOnly {
    u32 value;
};

namespace rstd
{
template<>
struct Impl<fmt::LowerHex, HexOnly> : ImplBase<HexOnly> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::LowerHex>(this->self().value).fmt(formatter);
    }
};
} // namespace rstd

TEST(Fmt, HexTraitDispatchAndWriterFailure) {
    EXPECT_EQ(rstd::format("{:#06x}", HexOnly { u32(42) }), "0x002a"_str);
    auto reject = [](void*, const rstd::uint8_t*, rstd::size_t) -> bool {
        return false;
    };
    fmt::Formatter formatter(nullptr, reject);
    auto           value = u32(42);
    EXPECT_FALSE(as<fmt::LowerHex>(value).fmt(formatter));

    auto options = fmt::FormattingOptions {};
    options.set_width(8).set_flag(fmt::FormattingOptions::ALTERNATE);
    options.set_presentation(fmt::Presentation::LowerHex);
    EXPECT_FALSE(options.is_debug());
    EXPECT_EQ(options.presentation(), fmt::Presentation::LowerHex);
    options.set_presentation(fmt::Presentation::UpperHex);
    EXPECT_FALSE(options.is_debug());
    EXPECT_EQ(options.presentation(), fmt::Presentation::UpperHex);
    options.set_presentation(fmt::Presentation::Debug);
    EXPECT_TRUE(options.is_debug());
    EXPECT_TRUE(options.alternate());
    EXPECT_EQ(options.width, 8);
}

TEST(Fmt, IntegerOctal) {
    EXPECT_EQ(rstd::format("{:o} {:#o} {:o} {:#o}", u32(255), u32(255), u8(), u64()),
              "377 0o377 0 0o0"_str);
    EXPECT_EQ(rstd::format("{:o} {:o} {:o}", 511, 64u, usize(8)), "777 100 10"_str);
    EXPECT_EQ(rstd::format("{:o} {:o} {:o} {:o}", i8(-1), i16(-1), i32(-1), i64(-1)),
              "377 177777 37777777777 1777777777777777777777"_str);
    EXPECT_EQ(rstd::format("{:o} {:o} {:o}", i8::MIN, i64::MIN, i128::MIN),
              "200 1000000000000000000000 2000000000000000000000000000000000000000000"_str);
    EXPECT_EQ(
        rstd::format("{:o} {:o}", u128::MAX, i128(-1)),
        "3777777777777777777777777777777777777777777 3777777777777777777777777777777777777777777"_str);
    EXPECT_EQ(rstd::format("{:o}", static_cast<rstd::int16_t>(-2)), "177776"_str);
}

TEST(Fmt, IntegerOctalPadding) {
    EXPECT_EQ(rstd::format("{:08o} {:#08o} {:+#08o}", u32(255), u32(255), u32(255)),
              "00000377 0o000377 +0o00377"_str);
    EXPECT_EQ(rstd::format("{:6o}|{:<6o}|{:_^8o}", u32(255), u32(255), u32(255)),
              "   377|377   |__377___"_str);
    EXPECT_EQ(rstd::format("{:0>#8o}|{:<#08o}", u32(255), u32(255)), "0000o377|0o000377"_str);
    EXPECT_EQ(rstd::format("{:#02o} {:+#06o} {:.5o}", u32(255), u8(), u32(255)),
              "0o377 +0o000 377"_str);
    EXPECT_EQ(rstd::format("{:+#08o}", i8(-1)), "+0o00377"_str);
}

struct OctalOnly {
    u32 value;
};

namespace rstd
{
template<>
struct Impl<fmt::Octal, OctalOnly> : ImplBase<OctalOnly> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Octal>(this->self().value).fmt(formatter);
    }
};
} // namespace rstd

TEST(Fmt, OctalTraitDispatchAndWriterFailure) {
    EXPECT_EQ(rstd::format("{:#06o}", OctalOnly { u32(255) }), "0o0377"_str);
    auto reject = [](void*, const rstd::uint8_t*, rstd::size_t) -> bool {
        return false;
    };
    fmt::Formatter formatter(nullptr, reject);
    auto           value = u32(255);
    EXPECT_FALSE(as<fmt::Octal>(value).fmt(formatter));

    auto options = fmt::FormattingOptions {};
    options.set_width(8).set_flag(fmt::FormattingOptions::ALTERNATE);
    options.set_presentation(fmt::Presentation::Octal);
    EXPECT_FALSE(options.is_debug());
    EXPECT_EQ(options.presentation(), fmt::Presentation::Octal);
    options.set_presentation(fmt::Presentation::UpperHex);
    EXPECT_EQ(options.presentation(), fmt::Presentation::UpperHex);
    options.set_presentation(fmt::Presentation::Octal);
    EXPECT_EQ(options.presentation(), fmt::Presentation::Octal);
    EXPECT_TRUE(options.alternate());
    EXPECT_EQ(options.width, 8);
}

TEST(Fmt, FloatDisplayAndDebug) {
    EXPECT_EQ(rstd::format("{} {} {}", f64(1.0), f64(1e20), f64(1e-7)),
              "1 100000000000000000000 0.0000001"_str);
    EXPECT_EQ(rstd::format("{:?} {:?} {:?} {:?}", f64(1.0), f64(1e16), f64(1e-4), f64(1e-5)),
              "1.0 1e16 0.0001 1e-5"_str);
    EXPECT_EQ(rstd::format("{}", f32(1.2345678f)), "1.2345678"_str);
}

TEST(Fmt, NativeDouble) {
    EXPECT_EQ(rstd::format("{} {:?} {:.3e} {:E}", 1.25, 1e-5, 42.0, 0.0042),
              "1.25 1e-5 4.200e1 4.2E-3"_str);
}

TEST(Fmt, FloatPrecisionAndExponent) {
    EXPECT_EQ(rstd::format("{:.3}", f64(1.25)), "1.250"_str);
    EXPECT_EQ(rstd::format("{:.3?}", f64(1.25)), "1.250"_str);
    EXPECT_EQ(rstd::format("{:e}", f64(42.0)), "4.2e1"_str);
    EXPECT_EQ(rstd::format("{:.3e}", f64(42.0)), "4.200e1"_str);
    EXPECT_EQ(rstd::format("{:E}", f64(0.0042)), "4.2E-3"_str);
    EXPECT_EQ(rstd::format("{:#}", f64(1.0)), "1"_str);
}

TEST(Fmt, FloatSpecialValuesAndSigns) {
    EXPECT_EQ(rstd::format("{} {:+} {}", f64::NAN_, f64::NAN_, -f64::NAN_), "NaN NaN NaN"_str);
    EXPECT_EQ(rstd::format("{} {:+} {}", f64::INFINITY_, f64::INFINITY_, f64::NEG_INFINITY),
              "inf +inf -inf"_str);
    EXPECT_EQ(rstd::format("{} {:?} {:e}", f64(-0.0), f64(-0.0), f64(-0.0)), "-0 -0.0 -0e0"_str);
}

TEST(Fmt, FloatPadding) {
    EXPECT_EQ(rstd::format("{:8.2}", f64(-1.5)), "   -1.50"_str);
    EXPECT_EQ(rstd::format("{:08.2}", f64(-1.5)), "-0001.50"_str);
    EXPECT_EQ(rstd::format("{:+08.2}", f64(1.5)), "+0001.50"_str);
    EXPECT_EQ(rstd::format("{:0>8.2}", f64(-1.5)), "000-1.50"_str);
    EXPECT_EQ(rstd::format("{:<08.2}", f64(-1.5)), "-0001.50"_str);
    EXPECT_EQ(rstd::format("{:_^9.2}", f64(1.5)), "__1.50___"_str);
}

TEST(Fmt, FloatLargePrecision) {
    auto formatted = rstd::format("{:.1200}", f64(1.0));
    ASSERT_EQ(formatted.size(), usize(1202));
    EXPECT_EQ(formatted.as_str()[usize()], u8('1'));
    EXPECT_EQ(formatted.as_str()[usize(1)], u8('.'));
    EXPECT_EQ(formatted.as_str()[formatted.size() - usize(1)], u8('0'));

    auto scientific = rstd::format("{:.1200e}", f64(1.0));
    ASSERT_EQ(scientific.size(), usize(1204));
    EXPECT_EQ(scientific.as_str()[scientific.size() - usize(2)], u8('e'));
    EXPECT_EQ(scientific.as_str()[scientific.size() - usize(1)], u8('0'));
}

TEST(Fmt, FloatBoundariesAndRounding) {
    const auto min_subnormal = rstd::bit_cast<f64>(u64(1));
    auto       fixed         = rstd::format("{}", min_subnormal);
    ASSERT_EQ(fixed.size(), usize(326));
    EXPECT_EQ(fixed.as_str()[usize()], u8('0'));
    EXPECT_EQ(fixed.as_str()[usize(1)], u8('.'));
    EXPECT_EQ(fixed.as_str()[fixed.size() - usize(1)], u8('5'));

    EXPECT_EQ(rstd::format("{:?}", min_subnormal), "5e-324"_str);
    EXPECT_EQ(rstd::format("{:?}", f64::MIN_POSITIVE), "2.2250738585072014e-308"_str);
    EXPECT_EQ(rstd::format("{:?}", f64::MAX), "1.7976931348623157e308"_str);
    EXPECT_EQ(rstd::format("{:?}", rstd::bit_cast<f32>(u32(1))), "1e-45"_str);
    EXPECT_EQ(rstd::format("{:?}", f32::MAX), "3.4028235e38"_str);

    EXPECT_EQ(rstd::format("{:.0} {:.0} {:.0} {:.0}", f64(0.5), f64(1.5), f64(2.5), f64(3.5)),
              "0 2 2 4"_str);
    EXPECT_EQ(rstd::format("{:.0} {:.0}", f64(9.5), f64(99.5)), "10 100"_str);
    EXPECT_EQ(rstd::format("{:.3e}", f64()), "0.000e0"_str);
    EXPECT_EQ(rstd::format("{:.900}", min_subnormal).size(), usize(902));
    EXPECT_EQ(rstd::format("{:.900}", f64::MIN_POSITIVE).size(), usize(902));
    EXPECT_EQ(rstd::format("{:.900e}", min_subnormal).size(), usize(907));
}

TEST(Fmt, Duration) {
    auto d = time::Duration::from_millis(u64(1500));
    auto s = rstd::format("Time: {:?}", d);
    EXPECT_EQ(s, "Time: 1.5s"_str);
}

TEST(Fmt, DurationMs) {
    auto d = time::Duration::from_millis(u64(500));
    auto s = rstd::format("{:?}", d);
    EXPECT_EQ(s, "500ms"_str);
}

TEST(Fmt, DurationNs) {
    auto d = time::Duration::from_nanos(u64(789));
    auto s = rstd::format("{:?}", d);
    EXPECT_EQ(s, "789ns"_str);
}
