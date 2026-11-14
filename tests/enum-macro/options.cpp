#include <rstd/test/gtest.hpp>

import rstd;
import rstd.serde;
import rstd.json;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace enum_macro::options
{
struct [[pmacro::derive("rstd::Debug", "serde::derive")]] ManualLabel {
    i32 value;
};

[[pmacro::invoke("rstd::Enum",
                 [[derive(Debug, "serde::derive")]] enum Labels {
                     [[debug(rename("UnitLabel"))]] Unit,
                     [[debug(rename("TupleLabel"))]] Tuple(i32),
                     [[debug(rename("RecordLabel"))]] Record { value: i32 },
                     [[debug(rename("ExternalLabel"))]] Adapted = ManualLabel,
                     [[debug(rename("EmptyTupleLabel"))]] EmptyTuple(),
                 })]];

TEST(EnumMacroOptions, RenamesUnitOwnedAndExternalDebugWithoutChangingMetadata) {
    EXPECT_EQ(rstd::format("{:?}", Labels::Unit()), "UnitLabel"_str);
    EXPECT_EQ(rstd::format("{:?}", Labels::Tuple(i32(3))), "TupleLabel(3)"_str);
    EXPECT_EQ(rstd::format("{:?}", Labels::Record(i32(4))), "RecordLabel { value: 4 }"_str);
    EXPECT_EQ(rstd::format("{:?}", Labels::Adapted(ManualLabel { i32(5) })),
              "ExternalLabel { value: 5 }"_str);
    EXPECT_EQ(rstd::PayloadDefinition<Labels::Tuple_payload>::name(), "Tuple"_str);
    EXPECT_EQ(rstd::PayloadDefinition<Labels::Record_payload>::name(), "Record"_str);
    static_assert(Labels::Unit_variant::name[0] == 'U');
    static_assert(sizeof(Labels::Unit_variant::name) == sizeof("Unit"));
    static_assert(Labels::Adapted_variant::name[0] == 'A');
    static_assert(sizeof(Labels::Adapted_variant::name) == sizeof("Adapted"));
}

TEST(EnumMacroOptions, DebugRenameDoesNotRenameSerdeCasesOrPayloadFields) {
    EXPECT_EQ(rstd::json::encode(Labels::Unit()).unwrap().as_str(), R"("Unit")"_str);
    EXPECT_EQ(rstd::json::encode(Labels::Tuple(i32(3))).unwrap().as_str(), R"({"Tuple":3})"_str);
    EXPECT_EQ(rstd::json::encode(Labels::Record(i32(4))).unwrap().as_str(),
              R"({"Record":{"value":4}})"_str);
    EXPECT_EQ(rstd::json::encode(Labels::Adapted(ManualLabel { i32(5) })).unwrap().as_str(),
              R"({"Adapted":{"value":5}})"_str);
    EXPECT_TRUE(rstd::json::decode<Labels>(R"("Unit")"_str).unwrap().is_Unit());
    EXPECT_TRUE(rstd::json::decode<Labels>(R"("UnitLabel")"_str).is_err());
}

TEST(EnumMacroOptions, RenamedEmptyTupleStillHasTupleDebugAndSerdeShape) {
    EXPECT_EQ(rstd::format("{:?}", Labels::EmptyTuple()), "EmptyTupleLabel()"_str);
    EXPECT_EQ(rstd::json::encode(Labels::EmptyTuple()).unwrap().as_str(),
              R"({"EmptyTuple":[]})"_str);
    EXPECT_EQ(rstd::PayloadDefinition<Labels::EmptyTuple_payload>::kind, rstd::VariantKind::Tuple);
}
} // namespace enum_macro::options
