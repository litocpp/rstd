#include <rstd/test/gtest.hpp>

import rstd.core;
import rstd.alloc;

using namespace rstd::literals;

namespace derive_tests::enums
{

enum class [[pmacro::derive("rstd::Debug",
                            "rstd::Display",
                            "rstd::FromStr",
                            "rstd::EnumIter",
                            "rstd::TryFrom")]] StrictState : int
{
    Idle = 0,
    Active [[pmacro::helper("rstd::Debug",
                            rename("ActiveDebug"))]] [[pmacro::helper("rstd::Display",
                                                                      rename("active-display"))]]
    [[pmacro::helper("rstd::FromStr", rename("active"), alias("running"), alias("go"))]] = 7,
    Stopped                                                                              = 19,
};

enum class [[pmacro::derive(
    "rstd::Debug",
    "rstd::Display",
    "rstd::FromStr",
    "rstd::EnumIter",
    "rstd::TryFrom")]] [[pmacro::helper("rstd::FromStr",
                                        ascii_case_insensitive)]] InsensitiveState : unsigned
{
    Idle = 2,
    Active [[pmacro::helper("rstd::Display", rename("in-progress"))]]
    [[pmacro::helper("rstd::FromStr", rename("in-progress"), alias("go"), alias("start"))]] = 11,
    Stopped                                                                                 = 29,
};

enum class [[pmacro::derive("rstd::Debug",
                            "rstd::Display",
                            "rstd::FromStr",
                            "rstd::EnumIter",
                            "rstd::TryFrom")]] UnsignedCode : rstd::uint64_t
{
    Low  = 1,
    High = 0x8000000000000000ull,
};

enum class [[pmacro::derive("rstd::EnumIter")]] EmptyState
{
};
enum class [[pmacro::derive("rstd::EnumIter")]] IterOnly
{
    Value = 3
};
enum class [[pmacro::derive("rstd::TryFrom")]] ConversionOnly
{
    Value = 5
};
enum class PlainCode : int
{
    Value = 2
};
enum class [[pmacro::derive("rstd::EnumIter", "rstd::TryFrom")]] SignedCode : signed char
{
    Low    = -128,
    Middle = -3,
    High   = 127
};
enum class [[pmacro::derive("rstd::EnumIter", "rstd::TryFrom")]] AliasCode : int
{
    Value = 4,
    Alias = Value,
    Last  = 99
};

static_assert(rstd::Impled<StrictState, rstd::EnumIter>);
static_assert(! rstd::Impled<StrictState, rstd::convert::From<rstd::i8>>);
static_assert(! rstd::Impled<StrictState, rstd::convert::From<int>>);
static_assert(! rstd::derive::DerivedEnumIter<ConversionOnly>);
static_assert(! rstd::convert::DerivedEnumTryFrom<IterOnly>);
static_assert(
    rstd::mtp::same_as<typename decltype(rstd::enum_iter<StrictState>())::Item, StrictState>);
static_assert(rstd::mtp::same_as<rstd::Impl<rstd::convert::TryFrom<rstd::i64>, StrictState>::Error,
                                 rstd::derive::TryFromEnumError<StrictState, rstd::i64>>);

TEST(DeriveEnum, IterationOwnsSparseValuesInDeclarationOrder) {
    auto values = rstd::enum_iter<StrictState>();
    EXPECT_EQ(values.next().unwrap(), StrictState::Idle);
    EXPECT_EQ(values.next().unwrap(), StrictState::Active);
    EXPECT_EQ(values.next().unwrap(), StrictState::Stopped);
    EXPECT_TRUE(values.next().is_none());
    EXPECT_TRUE(values.next().is_none());
    auto independent = rstd::enum_iter<StrictState>();
    EXPECT_EQ(independent.next().unwrap(), StrictState::Idle);

    auto empty = rstd::enum_iter<EmptyState>();
    EXPECT_TRUE(empty.next().is_none());
    auto aliases = rstd::enum_iter<AliasCode>();
    EXPECT_EQ(aliases.next().unwrap(), AliasCode::Value);
    EXPECT_EQ(aliases.next().unwrap(), AliasCode::Alias);
    EXPECT_EQ(aliases.next().unwrap(), AliasCode::Last);
    EXPECT_TRUE(aliases.next().is_none());
    EXPECT_EQ(rstd::try_into<AliasCode>(rstd::i32(4)).unwrap(), AliasCode::Value);
    EXPECT_EQ(rstd::try_into<ConversionOnly>(5).unwrap(), ConversionOnly::Value);
}

template<typename I>
void integer_conversions() {
    auto zero = I(0), active = I(7), stopped = I(19), gap = I(1);
    EXPECT_EQ(rstd::try_from<StrictState>(zero).unwrap(), StrictState::Idle);
    EXPECT_EQ(rstd::try_into<StrictState>(active).unwrap(), StrictState::Active);
    EXPECT_EQ(rstd::try_into<StrictState>(stopped).unwrap(), StrictState::Stopped);
    auto unknown = rstd::try_into<StrictState>(gap);
    ASSERT_TRUE(unknown.is_err());
    EXPECT_EQ(unknown.unwrap_err().value, gap);
}

TEST(DeriveEnum, CheckedConversionAcceptsOnlyDeclaredValuesForEveryIntegerWidth) {
    integer_conversions<rstd::i8>();
    integer_conversions<rstd::i16>();
    integer_conversions<rstd::i32>();
    integer_conversions<rstd::i64>();
    integer_conversions<rstd::i128>();
    integer_conversions<rstd::isize>();
    integer_conversions<rstd::u8>();
    integer_conversions<rstd::u16>();
    integer_conversions<rstd::u32>();
    integer_conversions<rstd::u64>();
    integer_conversions<rstd::u128>();
    integer_conversions<rstd::usize>();
    integer_conversions<int>();
    integer_conversions<unsigned>();
    integer_conversions<long long>();
    integer_conversions<unsigned long long>();
}

TEST(DeriveEnum, CheckedConversionRetainsOriginalValuesWithoutWrapping) {
    EXPECT_EQ(rstd::try_into<StrictState>(rstd::i64(-1)).unwrap_err().value, rstd::i64(-1));
    EXPECT_EQ(rstd::try_into<StrictState>(rstd::u64(4294967303ULL)).unwrap_err().value,
              rstd::u64(4294967303ULL));
    EXPECT_EQ(rstd::try_into<StrictState>(rstd::i128::MIN).unwrap_err().value, rstd::i128::MIN);
    EXPECT_EQ(rstd::try_into<StrictState>(rstd::u128::MAX).unwrap_err().value, rstd::u128::MAX);
    EXPECT_EQ(rstd::try_into<SignedCode>(-128).unwrap(), SignedCode::Low);
    EXPECT_EQ(rstd::try_into<SignedCode>(-3).unwrap(), SignedCode::Middle);
    EXPECT_EQ(rstd::try_into<SignedCode>(127u).unwrap(), SignedCode::High);
    EXPECT_EQ(rstd::try_into<SignedCode>(128u).unwrap_err().value, 128u);
    EXPECT_EQ(rstd::try_into<SignedCode>(-129).unwrap_err().value, -129);
    EXPECT_EQ(rstd::try_into<UnsignedCode>(rstd::u64(0x8000000000000000ULL)).unwrap(),
              UnsignedCode::High);
    EXPECT_EQ(rstd::try_into<UnsignedCode>(rstd::i64(-1)).unwrap_err().value, rstd::i64(-1));
    EXPECT_EQ(rstd::try_into<UnsignedCode>(rstd::u128::MAX).unwrap_err().value, rstd::u128::MAX);
    EXPECT_EQ(rstd::try_into<rstd::i32>(StrictState::Active).unwrap(), rstd::i32(7));
    EXPECT_EQ(rstd::try_into<StrictState>(StrictState::Active).unwrap(), StrictState::Active);
}

TEST(DeriveEnum, IntegerConversionErrorsImplementDisplayDebugAndError) {
    auto error = rstd::try_into<StrictState>(rstd::i64(3)).unwrap_err();
    EXPECT_EQ(rstd::format("{} {:?}", error, error),
              "Invalid StrictState: 3 Invalid StrictState: 3"_str);
    EXPECT_TRUE(rstd::as<rstd::error::Error>(error).source().is_none());
    EXPECT_EQ(rstd::from_str<StrictState>("running"_str).unwrap(), StrictState::Active);
    EXPECT_TRUE(rstd::from_str<StrictState>("7"_str).is_err());
}

TEST(DeriveEnum, CheckedConversionIsOptInAndLeavesNumericConversionsUnchanged) {
    EXPECT_EQ(rstd::into<PlainCode>(rstd::i8(2)), PlainCode::Value);
    EXPECT_EQ(static_cast<int>(rstd::try_into<PlainCode>(rstd::i8(3)).unwrap()), 3);
    EXPECT_TRUE(rstd::try_into<StrictState>(rstd::i8(3)).is_err());
    EXPECT_EQ(rstd::try_into<rstd::i8>(rstd::i64(127)).unwrap(), rstd::i8(127));
    EXPECT_TRUE(rstd::try_into<rstd::i8>(rstd::i64(128)).is_err());
    EXPECT_EQ(rstd::try_into<rstd::i32>(rstd::f64(7)).unwrap(), rstd::i32(7));
    EXPECT_TRUE(rstd::try_into<rstd::i32>(rstd::f64(7.5)).is_err());
}

static_assert(rstd::Impled<StrictState, rstd::fmt::Debug, rstd::fmt::Display, rstd::str_::FromStr>);
static_assert(
    rstd::Impled<InsensitiveState, rstd::fmt::Debug, rstd::fmt::Display, rstd::str_::FromStr>);
static_assert(rstd::mtp::same_as<decltype(rstd::from_str<StrictState>(""_str)),
                                 rstd::Result<StrictState, rstd::derive::ParseEnumError>>);
static_assert(
    rstd::mtp::same_as<
        decltype(rstd::derive::parse_enum<InsensitiveState>(""_str, rstd::derive::Case::Sensitive)),
        rstd::Result<InsensitiveState, rstd::derive::ParseEnumError>>);
static_assert(rstd::Impled<rstd::derive::ParseEnumError,
                           rstd::fmt::Display,
                           rstd::fmt::Debug,
                           rstd::error::Error>);

TEST(DeriveEnum, DefaultNamesAndIndependentTraitRenames) {
    EXPECT_EQ(rstd::format("{} {:?}", StrictState::Idle, StrictState::Idle), "Idle Idle"_str);
    EXPECT_EQ(rstd::format("{} {:?}", StrictState::Stopped, StrictState::Stopped),
              "Stopped Stopped"_str);
    EXPECT_EQ(rstd::format("{}", StrictState::Active), "active-display"_str);
    EXPECT_EQ(rstd::format("{:?}", StrictState::Active), "ActiveDebug"_str);

    EXPECT_EQ(rstd::from_str<StrictState>("active"_str).unwrap(), StrictState::Active);
    EXPECT_TRUE(rstd::from_str<StrictState>("ActiveDebug"_str).is_err());
    EXPECT_TRUE(rstd::from_str<StrictState>("active-display"_str).is_err());
    EXPECT_TRUE(rstd::from_str<StrictState>("Active"_str).is_err());
}

TEST(DeriveEnum, StrictFromStrAcceptsNamesAndAliases) {
    EXPECT_EQ(rstd::from_str<StrictState>("Idle"_str).unwrap(), StrictState::Idle);
    EXPECT_EQ(rstd::from_str<StrictState>("Stopped"_str).unwrap(), StrictState::Stopped);
    EXPECT_EQ(rstd::from_str<StrictState>("running"_str).unwrap(), StrictState::Active);
    EXPECT_EQ(rstd::from_str<StrictState>("go"_str).unwrap(), StrictState::Active);
    EXPECT_TRUE(rstd::from_str<StrictState>("idle"_str).is_err());
    EXPECT_TRUE(rstd::from_str<StrictState>("ACTIVE"_str).is_err());
    EXPECT_TRUE(rstd::from_str<StrictState>("Running"_str).is_err());
    EXPECT_TRUE(rstd::from_str<StrictState>("GO"_str).is_err());
}

TEST(DeriveEnum, StrictFromStrRejectsWhitespaceNumbersUnknownAndEmbeddedNull) {
    constexpr rstd::ref<rstd::str> invalid[] = {
        ""_str,
        " "_str,
        " active"_str,
        "active "_str,
        "\tactive"_str,
        "active\n"_str,
        "0"_str,
        "7"_str,
        "19"_str,
        "-1"_str,
        "+7"_str,
        "unknown"_str,
        "active\0extra"_str,
        "go\0"_str,
    };
    for (auto text : invalid) {
        EXPECT_TRUE(rstd::from_str<StrictState>(text).is_err());
        EXPECT_TRUE(
            rstd::derive::parse_enum<StrictState>(text, rstd::derive::Case::Sensitive).is_err());
    }
}

TEST(DeriveEnum, AsciiInsensitiveFromStrUsesRenamedNamesAndAliases) {
    EXPECT_EQ(rstd::from_str<InsensitiveState>("iDlE"_str).unwrap(), InsensitiveState::Idle);
    EXPECT_EQ(rstd::from_str<InsensitiveState>("IN-PrOgReSs"_str).unwrap(),
              InsensitiveState::Active);
    EXPECT_EQ(rstd::from_str<InsensitiveState>("GO"_str).unwrap(), InsensitiveState::Active);
    EXPECT_EQ(rstd::from_str<InsensitiveState>("StArT"_str).unwrap(), InsensitiveState::Active);
    EXPECT_EQ(rstd::from_str<InsensitiveState>("sToPpEd"_str).unwrap(), InsensitiveState::Stopped);
    EXPECT_TRUE(rstd::from_str<InsensitiveState>("Active"_str).is_err());
    EXPECT_TRUE(rstd::from_str<InsensitiveState>("\xc4\xb0"
                                                 "dle"_str)
                    .is_err());

    constexpr rstd::ref<rstd::str> invalid[] = {
        ""_str,   " "_str,  " idle"_str, "IDLE "_str,   "GO\n"_str,      "2"_str,
        "11"_str, "29"_str, "-1"_str,    "unknown"_str, "GO\0extra"_str,
    };
    for (auto text : invalid) EXPECT_TRUE(rstd::from_str<InsensitiveState>(text).is_err());
}

TEST(DeriveEnum, ExplicitStrictParserDoesNotInheritInsensitiveConfiguration) {
    using rstd::derive::Case;
    EXPECT_EQ(rstd::derive::parse_enum<InsensitiveState>("Idle"_str, Case::Sensitive).unwrap(),
              InsensitiveState::Idle);
    EXPECT_EQ(
        rstd::derive::parse_enum<InsensitiveState>("in-progress"_str, Case::Sensitive).unwrap(),
        InsensitiveState::Active);
    EXPECT_EQ(rstd::derive::parse_enum<InsensitiveState>("go"_str, Case::Sensitive).unwrap(),
              InsensitiveState::Active);
    EXPECT_EQ(rstd::derive::parse_enum<InsensitiveState>("start"_str, Case::Sensitive).unwrap(),
              InsensitiveState::Active);
    EXPECT_EQ(rstd::derive::parse_enum<InsensitiveState>("Stopped"_str, Case::Sensitive).unwrap(),
              InsensitiveState::Stopped);

    constexpr rstd::ref<rstd::str> invalid[] = {
        "idle"_str,  "IN-PROGRESS"_str, "GO"_str, "Start"_str, "STOPPED"_str,
        " Idle"_str, "go "_str,         "11"_str, ""_str,      "unknown"_str,
    };
    for (auto text : invalid) {
        EXPECT_TRUE(rstd::derive::parse_enum<InsensitiveState>(text, Case::Sensitive).is_err());
    }
}

TEST(DeriveEnum, DisplayNamesRoundTripWithoutEmittingAliases) {
    constexpr InsensitiveState values[] = {
        InsensitiveState::Idle,
        InsensitiveState::Active,
        InsensitiveState::Stopped,
    };
    for (auto value : values) {
        auto text = rstd::format("{}", value);
        EXPECT_EQ(rstd::from_str<InsensitiveState>(text.as_str()).unwrap(), value);
        EXPECT_EQ(
            rstd::derive::parse_enum<InsensitiveState>(text.as_str(), rstd::derive::Case::Sensitive)
                .unwrap(),
            value);
    }
    auto aliased = rstd::from_str<InsensitiveState>("StArT"_str).unwrap();
    EXPECT_EQ(rstd::format("{}", aliased), "in-progress"_str);
    EXPECT_EQ(rstd::format("{:?}", aliased), "Active"_str);
}

TEST(DeriveEnum, InvalidSignedAndUnsignedValuesIncludeTypeAndInteger) {
    auto negative = static_cast<StrictState>(-23);
    auto positive = static_cast<StrictState>(999);
    EXPECT_EQ(rstd::format("{}", negative), "StrictState(-23)"_str);
    EXPECT_EQ(rstd::format("{:?}", negative), "StrictState(-23)"_str);
    EXPECT_EQ(rstd::format("{} {:?}", positive, positive), "StrictState(999) StrictState(999)"_str);

    auto maximum = static_cast<UnsignedCode>(0xffffffffffffffffull);
    EXPECT_EQ(rstd::format("{}", maximum), "UnsignedCode(18446744073709551615)"_str);
    EXPECT_EQ(rstd::format("{:?}", maximum), "UnsignedCode(18446744073709551615)"_str);
    EXPECT_EQ(rstd::format("{} {:?}", UnsignedCode::High, UnsignedCode::High), "High High"_str);
    EXPECT_EQ(rstd::from_str<UnsignedCode>("High"_str).unwrap(), UnsignedCode::High);
    EXPECT_TRUE(rstd::from_str<UnsignedCode>("18446744073709551615"_str).is_err());
}

TEST(DeriveEnum, ParseErrorImplementsDisplayDebugAndError) {
    auto error = rstd::from_str<StrictState>("unknown"_str).unwrap_err();
    EXPECT_EQ(rstd::format("{}", error), "matching enum variant not found"_str);
    EXPECT_EQ(rstd::format("{:?}", error), "ParseEnumError"_str);
    EXPECT_TRUE(rstd::as<rstd::error::Error>(error).source().is_none());
}

TEST(DeriveEnum, ParseErrorDoesNotBorrowInput) {
    auto error = [] {
        auto input = alloc::string::String::make("temporary-unknown-name"_str);
        return rstd::from_str<InsensitiveState>(input.as_str()).unwrap_err();
    }();
    EXPECT_EQ(rstd::format("{}", error), "matching enum variant not found"_str);
    EXPECT_EQ(rstd::format("{:?}", error), "ParseEnumError"_str);
}

} // namespace derive_tests::enums
