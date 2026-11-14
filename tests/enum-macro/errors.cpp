#include <rstd/test/gtest.hpp>

import rstd.tests.enum_macro.fixture;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using alloc::sync::Arc;
using namespace enum_macro::fixture;

static_assert(! rstd::mtp::copy<Failure>);
static_assert(rstd::Impled<Failure, rstd::error::Error, rstd::fmt::Debug, rstd::clone::Clone>);
static_assert(rstd::Impled<Failure, rstd::convert::From<Leaf>>);
static_assert(! rstd::Impled<Failure, rstd::convert::From<Arc<Leaf>>>);

TEST(EnumMacroError, KeepsDisplayAndStructuralDebugSeparateForEveryPayloadShape) {
    auto unit = Failure::Unit();
    EXPECT_EQ(rstd::format("{} {:?}", unit, unit), "unit Unit"_str);
    auto nested = Failure::Nested(Leaf { "bad"_Str });
    EXPECT_EQ(rstd::format("{}", nested), "leaf bad"_str);
    EXPECT_EQ(rstd::format("{:?}", nested), "Nested(Leaf { message: \"bad\" })"_str);
    auto named = Failure::Named("bad"_Str);
    EXPECT_EQ(rstd::format("{} {:?}", named, named), "bad Named { message: \"bad\" }"_str);
    auto empty_tuple = Failure::EmptyTuple();
    EXPECT_EQ(rstd::format("{} {:?}", empty_tuple, empty_tuple), "empty tuple EmptyTuple()"_str);
    auto empty_record = Failure::EmptyRecord();
    EXPECT_EQ(rstd::format("{} {:?}", empty_record, empty_record),
              "empty record EmptyRecord {}"_str);
    EXPECT_EQ(rstd::format("{}", Failure::custom()), "custom"_str);
    EXPECT_EQ(rstd::format("{}", Failure::Number(i32(7))), "number 7"_str);
    EXPECT_EQ(rstd::format("{}", Failure::Numbers(i32(3), i32(4))), "3:4"_str);
}

TEST(EnumMacroError, ConsumesMoveOnlyFromSourceAndBorrowsItsExactStoredObject) {
    auto error = rstd::into<Failure>(Leaf { "source"_Str });
    ASSERT_TRUE(error.is_Nested());
    auto source = rstd::as<rstd::error::Error>(error).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Leaf>(*source));
    EXPECT_EQ(source->as_raw_ptr(), &error.as_Nested().cause);
    EXPECT_TRUE((*source)->source().is_none());
    auto cloned        = rstd::as<rstd::clone::Clone>(error).clone();
    auto cloned_source = rstd::as<rstd::error::Error>(cloned).source();
    ASSERT_TRUE(cloned_source.is_some());
    EXPECT_EQ(cloned_source->as_raw_ptr(), &cloned.as_Nested().cause);
    EXPECT_NE(cloned_source->as_raw_ptr(), source->as_raw_ptr());
}

TEST(EnumMacroError, UnwrapsSharedAndOptionalSourceOwnersWithoutAddingNodes) {
    auto cause   = Arc<Leaf>::make(Leaf { "shared"_Str });
    auto address = &*cause;
    auto error   = SourceFailure::Shared(cause.clone());
    auto source  = rstd::as<rstd::error::Error>(error).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Leaf>(*source));
    EXPECT_EQ(source->as_raw_ptr(), address);
    auto optional        = SourceFailure::Optional(Some(rstd::move(cause)));
    auto optional_source = rstd::as<rstd::error::Error>(optional).source();
    ASSERT_TRUE(optional_source.is_some());
    EXPECT_EQ(optional_source->as_raw_ptr(), address);
    auto absent = SourceFailure::Optional(None());
    EXPECT_TRUE(rstd::as<rstd::error::Error>(absent).source().is_none());
}

TEST(EnumMacroError, TransparentDelegatesDisplayAndTheUnderlyingSource) {
    auto error = SourceFailure::Transparent(NestedLeaf { Leaf { "root"_Str } });
    EXPECT_EQ(rstd::format("{}", error), "nested leaf root"_str);
    auto source = rstd::as<rstd::error::Error>(error).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Leaf>(*source));
    EXPECT_EQ(source->as_raw_ptr(), &error.as_Transparent().field_0.cause);
}

TEST(EnumMacroError, DelegatesExternalPayloadDisplayAndCustomSource) {
    auto cause   = Arc<Leaf>::make(Leaf { "native"_Str });
    auto address = &*cause;
    auto error   = AdaptedFailure::Adapted(ManualPayload { "adapted"_Str, rstd::move(cause) });
    EXPECT_EQ(rstd::format("{}", error), "adapted"_str);
    auto source = rstd::as<rstd::error::Error>(error).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Leaf>(*source));
    EXPECT_EQ(source->as_raw_ptr(), address);
}

TEST(EnumMacroError, PreservesRecursiveSourceIdentityAndFormatEscapesAcrossImports) {
    auto root    = Arc<Recursive>::make(Recursive::Root());
    auto address = &*root;
    auto error   = Recursive::Context("opening"_Str, rstd::move(root));
    EXPECT_EQ(rstd::format("{}", error), "root\nopening"_str);
    EXPECT_EQ(rstd::format("{:?}", error), "Context { context: \"opening\", error: Root }"_str);
    auto source = rstd::as<rstd::error::Error>(error).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Recursive>(*source));
    EXPECT_EQ(source->as_raw_ptr(), address);
    EXPECT_TRUE((*source)->source().is_none());
    auto cloned        = rstd::as<rstd::clone::Clone>(error).clone();
    auto cloned_source = rstd::as<rstd::error::Error>(cloned).source();
    ASSERT_TRUE(cloned_source.is_some());
    EXPECT_EQ(cloned_source->as_raw_ptr(), address);
}
