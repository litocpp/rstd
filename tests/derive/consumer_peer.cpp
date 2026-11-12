#include <rstd/test/gtest.hpp>

import rstd.core;
import rstd.alloc;
import rstd.tests.derive.fixture;

using namespace rstd::literals;

namespace derive_consumer
{

auto number_from_peer(rstd::ref<rstd::str> text)
    -> rstd::Result<derive_fixture::ExportedNumber, rstd::num::ParseIntError> {
    return rstd::from_str<derive_fixture::ExportedNumber>(text);
}

auto state_from_peer(rstd::ref<rstd::str> text)
    -> rstd::Result<derive_fixture::nested::ExportedState, rstd::derive::ParseEnumError> {
    return rstd::from_str<derive_fixture::nested::ExportedState>(text);
}

TEST(DeriveModule, SecondTranslationUnitFormatsImportedTypesIndependently) {
    const auto number = derive_fixture::ExportedNumber { rstd::i32(8) };
    EXPECT_EQ(rstd::format("{}", number), "8"_str);
    EXPECT_EQ(rstd::format("{:#?}", number), "ExportedNumber {\n    value: 8,\n}"_str);

    auto state = rstd::from_str<derive_fixture::nested::ExportedState>("oK"_str).unwrap();
    EXPECT_EQ(rstd::format("{} {:?}", state, state), "ready Ready"_str);
    EXPECT_TRUE(rstd::derive::parse_enum<derive_fixture::nested::ExportedState>(
                    "oK"_str, rstd::derive::Case::Sensitive)
                    .is_err());

    const auto summary = derive_fixture::nested::ExportedSummary { rstd::i32(2), "peer"_str };
    EXPECT_EQ(rstd::format("{}", summary), "peer: 2"_str);
    EXPECT_EQ(rstd::format("{:?}", summary), "ExportedSummary { count: 2, name: \"peer\" }"_str);
}

} // namespace derive_consumer
