#include <rstd/test/gtest.hpp>

import rstd.core;
import rstd.alloc;
import rstd.tests.derive.errors;

using namespace rstd::literals;
using derive_fixture::errors::Cause;
using derive_fixture::errors::Failure;
using derive_fixture::errors::Recursive;
using derive_fixture::errors::SharedFailure;

static_assert(! rstd::mtp::copy<Failure>);
static_assert(rstd::Impled<Failure, rstd::error::Error, rstd::convert::From<Cause>>);

TEST(DeriveErrorModule, IndependentImporterUsesTraitsAndConversionConstructor) {
    auto value = rstd::into<Failure>(Cause { "owned"_Str });
    ASSERT_TRUE(value.is_Wrapped());
    EXPECT_EQ(rstd::format("{}", value), "cause owned\ncontext\r\t\\\""_str);
    EXPECT_EQ(rstd::format("{:?}", value), "Wrapped(Cause { value: \"owned\" })"_str);
    auto source = rstd::as<rstd::error::Error>(value).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Cause>(*source));
    EXPECT_EQ(source->as_raw_ptr(), &value.as_Wrapped().cause);
    auto direct = Failure(Cause { "constructor"_Str });
    EXPECT_EQ(direct.as_Wrapped().cause.value.as_str(), "constructor"_str);
    auto unit = Failure::Unit();
    EXPECT_EQ(rstd::format("{} {:?}", unit, unit), "not the variant name Unit"_str);
}

TEST(DeriveErrorModule, RecursiveRootAndContextApiKeepOwnedChainAcrossImports) {
    static_assert(! rstd::mtp::copy<Recursive>);
    static_assert(! rstd::Impled<Recursive, rstd::clone::Clone>);
    auto value = rstd::into<Recursive>(Cause { "root"_Str });
    EXPECT_EQ(&value.root(), &value);
    auto        inner    = rstd::move(value).with_context("inner"_str);
    auto        error    = rstd::move(inner).with_context("outer"_str);
    const auto* expected = &*error.as_WithContext().error->as_WithContext().error;
    EXPECT_EQ(&error.root(), expected);
    ASSERT_TRUE(error.root().is_Leaf());
    EXPECT_EQ(error.root().as_Leaf().cause.value.as_str(), "root"_str);
    EXPECT_EQ(rstd::format("{}", error), "cause root\ninner\nouter"_str);
    EXPECT_EQ(
        rstd::format("{:?}", error),
        "WithContext { context: \"outer\", error: WithContext { context: \"inner\", error: Leaf(Cause { value: \"root\" }) } }"_str);
    auto source = rstd::as<rstd::error::Error>(error).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Recursive>(*source));
    EXPECT_EQ(source->as_raw_ptr(), &*error.as_WithContext().error);
    auto root_source = (*source)->source();
    ASSERT_TRUE(root_source.is_some());
    EXPECT_EQ(root_source->as_raw_ptr(), expected);
    auto cause = (*root_source)->source();
    ASSERT_TRUE(cause.is_some());
    EXPECT_TRUE(rstd::error::is<Cause>(*cause));
    auto unit = Recursive::Unit().with_context("unit context"_str);
    EXPECT_TRUE(unit.root().is_Unit());
    EXPECT_EQ(rstd::format("{}", unit), "unit error\nunit context"_str);
}

TEST(DeriveErrorModule, OutOfClassMatchMethodsCloneOnlySharedOwners) {
    using alloc::sync::Arc;
    auto owner    = Arc<Cause>::make(Cause { "shared"_Str });
    auto address  = &*owner;
    auto original = rstd::into<SharedFailure>(rstd::move(owner));
    auto copy     = original.clone();
    ASSERT_TRUE(copy.is_Shared());
    EXPECT_TRUE(copy.has_cause());
    EXPECT_TRUE(original.has_cause());
    EXPECT_EQ(&*copy.as_Shared().cause, address);
    EXPECT_EQ(&*original.as_Shared().cause, address);
    EXPECT_EQ(rstd::format("{} {:?}", copy, copy),
              "cause shared Shared(Cause { value: \"shared\" })"_str);
    auto source = rstd::as<rstd::error::Error>(copy).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Cause>(*source));
    EXPECT_EQ(source->as_raw_ptr(), address);
    auto unit = SharedFailure::Unit().clone();
    EXPECT_TRUE(unit.is_Unit());
    EXPECT_FALSE(unit.has_cause());
    EXPECT_EQ(rstd::format("{} {:?}", unit, unit), "shared unit Unit"_str);
}
