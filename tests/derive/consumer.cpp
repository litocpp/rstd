#include <rstd/test/gtest.hpp>

import rstd.core;
import rstd.alloc;
import rstd.tests.derive.fixture;

using namespace rstd::literals;

namespace derive_consumer
{

using Number  = derive_fixture::ExportedNumber;
using State   = derive_fixture::nested::ExportedState;
using Summary = derive_fixture::nested::ExportedSummary;

auto number_from_peer(rstd::ref<rstd::str> text) -> rstd::Result<Number, rstd::num::ParseIntError>;
auto state_from_peer(rstd::ref<rstd::str> text)
    -> rstd::Result<State, rstd::derive::ParseEnumError>;

static_assert(rstd::Impled<Number, rstd::fmt::Debug, rstd::fmt::Display, rstd::str_::FromStr>);
static_assert(rstd::Impled<State, rstd::fmt::Debug, rstd::fmt::Display, rstd::str_::FromStr>);
static_assert(rstd::Impled<Summary, rstd::fmt::Debug, rstd::fmt::Display>);

TEST(DeriveModule, SeparateExportClassMakesAllTraitsReachable) {
    auto value = rstd::from_str<Number>("42"_str).unwrap();
    EXPECT_EQ(value.value, rstd::i32(42));
    EXPECT_EQ(rstd::format("{}", value), "42"_str);
    EXPECT_EQ(rstd::format("{:+06}", value), "+00042"_str);
    EXPECT_EQ(rstd::format("{:?}", value), "ExportedNumber { value: 42 }"_str);
    EXPECT_TRUE(rstd::from_str<Number>("invalid"_str).unwrap_err().kind()->is_InvalidDigit());
}

TEST(DeriveModule, ExportBlockInNestedNamespaceMakesEnumTraitsReachable) {
    auto iterator = rstd::enum_iter<State>();
    EXPECT_EQ(iterator.next().unwrap(), State::Idle);
    EXPECT_EQ(iterator.next().unwrap(), State::Ready);
    EXPECT_TRUE(iterator.next().is_none());
    EXPECT_EQ(rstd::try_into<State>(rstd::i32(1)).unwrap(), State::Ready);
    EXPECT_TRUE(rstd::try_into<State>(rstd::i32(2)).is_err());
    EXPECT_EQ(rstd::format("{} {:?}", State::Ready, State::Ready), "ready Ready"_str);
    EXPECT_EQ(rstd::from_str<State>("ReAdY"_str).unwrap(), State::Ready);
    EXPECT_EQ(rstd::from_str<State>("OK"_str).unwrap(), State::Ready);
    EXPECT_EQ(rstd::derive::parse_enum<State>("ready"_str, rstd::derive::Case::Sensitive).unwrap(),
              State::Ready);
    EXPECT_TRUE(
        rstd::derive::parse_enum<State>("READY"_str, rstd::derive::Case::Sensitive).is_err());
}

TEST(DeriveModule, ExportBlockRecordKeepsNamedDisplayAndPrettyDebug) {
    const auto value = Summary { rstd::i32(3), "module"_str };
    EXPECT_EQ(rstd::format("{}", value), "module: 3"_str);
    EXPECT_EQ(rstd::format("{:?}", value), "ExportedSummary { count: 3, name: \"module\" }"_str);
    EXPECT_EQ(rstd::format("{:#?}", value),
              "ExportedSummary {\n    count: 3,\n    name: \"module\",\n}"_str);
}

TEST(DeriveModule, ValuesParsedInAnotherTranslationUnitUseSameTraits) {
    auto number = number_from_peer("-17"_str).unwrap();
    EXPECT_EQ(number.value, rstd::i32(-17));
    EXPECT_EQ(rstd::format("{}", number), "-17"_str);
    EXPECT_EQ(rstd::format("{:?}", number), "ExportedNumber { value: -17 }"_str);
    auto state = state_from_peer("OK"_str).unwrap();
    EXPECT_EQ(state, State::Ready);
    EXPECT_EQ(rstd::format("{} {:?}", state, state), "ready Ready"_str);
    auto error = state_from_peer("unknown"_str).unwrap_err();
    EXPECT_EQ(rstd::format("{}", error), "matching enum variant not found"_str);
}

} // namespace derive_consumer
