#include <rstd/test/gtest.hpp>

import rstd.tests.enum_macro.fixture;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace enum_macro::fixture;

TEST(EnumMacroModule, ImportsDefinitionsAndTraitsFromTwoTranslationUnits) {
    auto value = message_from_peer();
    ASSERT_TRUE(value.is_Named());
    EXPECT_EQ(value.as_Named().count, i32(8));
    EXPECT_EQ(rstd::format("{:?}", value), "Named { count: 8, text: \"peer\" }"_str);
    auto failure = failure_from_peer();
    ASSERT_TRUE(failure.is_Nested());
    EXPECT_EQ(rstd::format("{}", failure), "leaf peer"_str);
    auto cloned = rstd::as<rstd::clone::Clone>(failure).clone();
    auto source = rstd::as<rstd::error::Error>(cloned).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_EQ(source->as_raw_ptr(), &cloned.as_Nested().cause);
}

TEST(EnumMacroModule, ImportsNestedExportBlocksAndAnonymousNamespaceHelpers) {
    nested::ExportBlock value;
    EXPECT_TRUE(value.is_Idle());
    value.replace_Text("exported"_Str);
    auto copy = rstd::as<rstd::clone::Clone>(value).clone();
    EXPECT_EQ(copy.as_Text().field_0.as_str(), "exported"_str);
    EXPECT_EQ(rstd::format("{:?}", copy), "Text(\"exported\")"_str);
    EXPECT_EQ(enum_macro_local_value(), i32(9));
}

TEST(EnumMacroModule, ImportsTemplateAndSerdeDefinitions) {
    auto original = Maybe<String>::Some("template"_Str);
    auto copy     = rstd::as<rstd::clone::Clone>(original).clone();
    ASSERT_TRUE(copy.is_Some());
    EXPECT_EQ(copy.as_Some().value.as_str(), "template"_str);
    auto decoded = decode_from_peer("wire"_str).unwrap();
    EXPECT_EQ(decoded.as_Text().field_0.as_str(), "wire"_str);
}
