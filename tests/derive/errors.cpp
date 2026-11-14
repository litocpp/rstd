#include <rstd/enum.hpp>
#include <rstd/test/gtest.hpp>

import rstd.core;
import rstd.alloc;

using namespace rstd::prelude;
using namespace rstd::literals;
using alloc::boxed::Box;
using alloc::sync::Arc;
using alloc::string::String;
using alloc::collections::BTreeMap;
using alloc::vec::Vec;

namespace derive_tests::errors
{
struct [[pmacro::derive("rstd::Debug")]] Settings {
    BTreeMap<String, String> values;
};

struct [[pmacro::derive("rstd::Debug")]] DebugHeader {
    String                name;
    rstd::slice<rstd::u8> value;
};

class Recursive;

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("{cause}: {message}"))]] RecursiveContext {
    String message;
    [[pmacro::helper("rstd::Error", source)]]
    Arc<Recursive> cause;
};

class [[pmacro::derive("rstd::Debug", "rstd::Error"),
        pmacro::helper("rstd::Debug", variants),
        pmacro::helper("rstd::Error", variants, unit("End", "root"))]] Recursive {
    RSTD_ENUM_TYPES(Recursive, (End), (WithContext, (RecursiveContext)))
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("leaf {message}"))]] Leaf {
    String message;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Debug", tuple),
         pmacro::helper("rstd::Error", format("nested: {cause}"))]] Nested {
    [[pmacro::helper("rstd::Error", from)]]
    Leaf cause;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"), pmacro::helper("rstd::Error", transparent)]]
Transparent {
    [[pmacro::helper("rstd::Error", from)]]
    Nested cause;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("{message}: {cause}"))]] Context {
    String message;
    [[pmacro::helper("rstd::Error", source)]]
    Arc<Leaf> cause;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("optional"))]] Optional {
    [[pmacro::helper("rstd::Error", source)]]
    Option<Box<rstd::dyn<rstd::error::Error>>> cause;
};

class [[pmacro::derive("rstd::Debug", "rstd::Error"),
        pmacro::helper("rstd::Debug", variants),
        pmacro::helper("rstd::Error", variants, unit("Expired", "session expired"))]] Failure {
    RSTD_ENUM_TYPES(Failure,
                    (Expired),
                    (Nested, (::derive_tests::errors::Nested)),
                    (Context, (::derive_tests::errors::Context)),
                    (Transparent, (::derive_tests::errors::Transparent)))
};

class [[pmacro::derive("rstd::Debug", "rstd::Display"),
        pmacro::helper("rstd::Debug", variants),
        pmacro::helper("rstd::Display", variants, unit("Unit", "unit text"))]] DisplayOnly {
    RSTD_ENUM_TYPES(DisplayOnly, (Unit), (Value, (Leaf)), (Number, (rstd::i32)))
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"), pmacro::helper("rstd::Error", transparent)]]
TransparentLeaf {
    Leaf cause;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("named source"))]] NamedSource {
    Leaf source;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("explicit from"))]] ExplicitFrom {
    [[pmacro::helper("rstd::Error", from)]]
    Leaf cause;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Display"),
         pmacro::helper("rstd::Display", format("{message}"))]] ManualSource {
    String                             message;
    Box<rstd::dyn<rstd::error::Error>> cause;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("optional shared"))]] OptionalArc {
    [[pmacro::helper("rstd::Error", source)]]
    Option<Arc<Leaf>> cause;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("typed box"))]] TypedBox {
    [[pmacro::helper("rstd::Error", source)]]
    Box<Leaf> cause;
};
} // namespace derive_tests::errors

namespace rstd
{
template<>
struct Impl<error::Error, derive_tests::errors::ManualSource>
    : ImplBase<derive_tests::errors::ManualSource> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        return Some(this->self().cause.as_ref());
    }
};

template<>
struct Impl<convert::From<derive_tests::errors::Leaf>, derive_tests::errors::ExplicitFrom> {
    static auto from(derive_tests::errors::Leaf value) -> derive_tests::errors::ExplicitFrom {
        value.message = "domain conversion"_Str;
        return { rstd::move(value) };
    }
};
} // namespace rstd

namespace derive_tests::errors
{
class [[pmacro::derive("rstd::Debug", "rstd::Error"),
        pmacro::helper("rstd::Debug", variants),
        pmacro::helper("rstd::Error", variants)]] ManualFailure {
    RSTD_ENUM_TYPES(ManualFailure, (Adapted, (ManualSource)))
};

static_assert(! rstd::mtp::copy<Failure>);
static_assert(! rstd::Impled<Failure, rstd::clone::Clone>);
static_assert(rstd::Impled<Failure, rstd::convert::From<Leaf>>);
static_assert(! rstd::Impled<Failure, rstd::convert::From<Arc<Leaf>>>);

TEST(DeriveError, UnitTupleAndNamedVariantsKeepStructuralDebug) {
    auto unit = Failure::Expired();
    EXPECT_EQ(rstd::format("{} {:?}", unit, unit), "session expired Expired"_str);
    auto nested = Failure::Nested(Nested { Leaf { "broken"_Str } });
    EXPECT_EQ(rstd::format("{}", nested), "nested: leaf broken"_str);
    EXPECT_EQ(rstd::format("{:?}", nested), "Nested(Leaf { message: \"broken\" })"_str);
    EXPECT_EQ(rstd::format("{:#?}", nested),
              "Nested(\n    Leaf {\n        message: \"broken\",\n    },\n)"_str);
    auto context = Failure::Context(Context { "opening"_Str, Arc<Leaf>::make(Leaf { "bad"_Str }) });
    EXPECT_EQ(rstd::format("{:?}", context),
              "Context { message: \"opening\", cause: Leaf { message: \"bad\" } }"_str);
    auto display = DisplayOnly::Value(Leaf { "display"_Str });
    EXPECT_EQ(rstd::format("{} {:?}", display, display),
              "leaf display Value { message: \"display\" }"_str);
    EXPECT_EQ(rstd::format("{}", DisplayOnly::Unit()), "unit text"_str);
    EXPECT_EQ(rstd::format("{:?}", DisplayOnly::Number(rstd::i32(7))), "Number(7)"_str);
}

TEST(DeriveError, ExplicitSourcesBorrowUnderlyingObjectsWithoutWrapperNodes) {
    auto failure = Failure::Nested(Nested { Leaf { "nested"_Str } });
    auto source  = rstd::as<rstd::error::Error>(failure).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Leaf>(*source));
    EXPECT_EQ(source->as_raw_ptr(), &failure.as_Nested().cause);
    auto cause    = Arc<Leaf>::make(Leaf { "arc"_Str });
    auto expected = &*cause;
    auto context  = Failure::Context(Context { "opening"_Str, rstd::move(cause) });
    auto borrowed = rstd::as<rstd::error::Error>(context).source();
    ASSERT_TRUE(borrowed.is_some());
    EXPECT_TRUE(rstd::error::is<Leaf>(*borrowed));
    EXPECT_EQ(borrowed->as_raw_ptr(), expected);
    auto empty = Optional {};
    EXPECT_TRUE(rstd::as<rstd::error::Error>(empty).source().is_none());
    auto boxed      = Box<rstd::dyn<rstd::error::Error>>::make(Leaf { "box"_Str });
    auto address    = boxed.as_ref().as_raw_ptr();
    auto optional   = Optional { Some(rstd::move(boxed)) };
    auto box_source = rstd::as<rstd::error::Error>(optional).source();
    ASSERT_TRUE(box_source.is_some());
    EXPECT_TRUE(rstd::error::is<Leaf>(*box_source));
    EXPECT_EQ(box_source->as_raw_ptr(), address);
}

TEST(DeriveError, TransparentForwardsUnderlyingSourceInsteadOfUnderlyingNode) {
    auto value = Failure::Transparent(Transparent { Nested { Leaf { "transparent"_Str } } });
    EXPECT_EQ(rstd::format("{}", value), "nested: leaf transparent"_str);
    auto source = rstd::as<rstd::error::Error>(value).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Leaf>(*source));
    EXPECT_EQ(source->as_raw_ptr(), &value.as_Transparent().cause.cause);
    auto leaf = TransparentLeaf { Leaf { "leaf"_Str } };
    EXPECT_EQ(rstd::format("{}", leaf), "leaf leaf"_str);
    EXPECT_TRUE(rstd::as<rstd::error::Error>(leaf).source().is_none());
}

TEST(DeriveError, FromConsumesMoveOnlySourceAndSelectsItsVariant) {
    auto value = rstd::into<Failure>(Leaf { "moved"_Str });
    ASSERT_TRUE(value.is_Nested());
    EXPECT_EQ(value.as_Nested().cause.message.as_str(), "moved"_str);
    auto wrapper = rstd::into<Transparent>(Nested { Leaf { "direct"_Str } });
    EXPECT_EQ(wrapper.cause.cause.message.as_str(), "direct"_str);
    auto custom = rstd::into<ExplicitFrom>(Leaf { "original"_Str });
    EXPECT_EQ(custom.cause.message.as_str(), "domain conversion"_str);
    auto named  = NamedSource { Leaf { "named"_Str } };
    auto source = rstd::as<rstd::error::Error>(named).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_EQ(source->as_raw_ptr(), &named.source);
}

TEST(DeriveError, CustomPayloadSourcesAndOptionalOwnersKeepBorrowedIdentity) {
    auto boxed    = Box<rstd::dyn<rstd::error::Error>>::make(Leaf { "custom"_Str });
    auto expected = boxed.as_ref().as_raw_ptr();
    auto manual   = ManualFailure::Adapted(ManualSource { "adapted"_Str, rstd::move(boxed) });
    auto borrowed = rstd::as<rstd::error::Error>(manual).source();
    ASSERT_TRUE(borrowed.is_some());
    EXPECT_EQ(borrowed->as_raw_ptr(), expected);
    EXPECT_EQ(rstd::format("{} {:?}", manual, manual),
              "adapted Adapted { message: \"adapted\", cause: Leaf { message: \"custom\" } }"_str);

    auto absent = OptionalArc {};
    EXPECT_TRUE(rstd::as<rstd::error::Error>(absent).source().is_none());
    auto shared          = Arc<Leaf>::make(Leaf { "shared"_Str });
    auto address         = &*shared;
    auto optional        = OptionalArc { Some(rstd::move(shared)) };
    auto optional_source = rstd::as<rstd::error::Error>(optional).source();
    ASSERT_TRUE(optional_source.is_some());
    EXPECT_EQ(optional_source->as_raw_ptr(), address);

    auto typed        = TypedBox { Box<Leaf>::make(Leaf { "typed"_Str }) };
    auto typed_source = rstd::as<rstd::error::Error>(typed).source();
    ASSERT_TRUE(typed_source.is_some());
    EXPECT_EQ(typed_source->as_raw_ptr(), &*typed.cause);
    EXPECT_EQ(rstd::format("{:?}", typed), "TypedBox { cause: Leaf { message: \"typed\" } }"_str);
    EXPECT_EQ(rstd::format("{}", typed.cause), "leaf typed"_str);
}

TEST(DeriveError, RecursivePayloadDefersTraitsUntilOuterTypeIsComplete) {
    auto cause   = Arc<Recursive>::make(Recursive::End());
    auto address = &*cause;
    auto value   = Recursive::WithContext(RecursiveContext { "context"_Str, rstd::move(cause) });
    EXPECT_EQ(rstd::format("{}", value), "root: context"_str);
    EXPECT_EQ(rstd::format("{:?}", value), "WithContext { message: \"context\", cause: End }"_str);
    auto source = rstd::as<rstd::error::Error>(value).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Recursive>(*source));
    EXPECT_EQ(source->as_raw_ptr(), address);
    EXPECT_TRUE((*source)->source().is_none());
}

TEST(DeriveError, OptionalAndSharedDynErrorsRetainStructuralDebug) {
    using DynError = rstd::dyn<rstd::error::Error>;
    static_assert(rstd::Impled<Box<DynError>, rstd::fmt::Debug, rstd::fmt::Display>);
    static_assert(
        rstd::Impled<Arc<DynError>, rstd::fmt::Debug, rstd::fmt::Display, rstd::error::Error>);
    static_assert(rstd::Impled<Option<Box<DynError>>, rstd::fmt::Debug>);
    auto absent = Optional {};
    EXPECT_EQ(rstd::format("{:?}", absent), "Optional { cause: None }"_str);
    auto boxed = Optional { Some(Box<DynError>::make(Leaf { "owned"_Str })) };
    EXPECT_EQ(rstd::format("{:?}", boxed),
              "Optional { cause: Some(Leaf { message: \"owned\" }) }"_str);
    EXPECT_EQ(
        rstd::format("{:#?}", boxed),
        "Optional {\n    cause: Some(\n        Leaf {\n            message: \"owned\",\n        },\n    ),\n}"_str);
    auto shared = Arc<DynError>::make(Leaf { "shared"_Str });
    EXPECT_EQ(rstd::format("{} {:?}", shared, shared),
              "leaf shared Leaf { message: \"shared\" }"_str);
    auto cloned = shared.clone();
    EXPECT_EQ(cloned.deref().as_raw_ptr(), shared.deref().as_raw_ptr());
    EXPECT_TRUE(rstd::as<rstd::error::Error>(shared).source().is_none());
    auto erased = Box<DynError>::make(Nested { Leaf { "root"_Str } });
    auto arc    = Arc<DynError>::make(Nested { Leaf { "root"_Str } });
    auto source = rstd::as<rstd::error::Error>(arc).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<Leaf>(*source));
    EXPECT_EQ(rstd::format("{}", *source), "leaf root"_str);
    Option<String> text = Some("jump"_Str);
    EXPECT_EQ(rstd::format("{:?}", text), "Some(\"jump\")"_str);
}

TEST(DeriveDebug, BTreeMapFormatsEveryFieldInKeyOrderWithoutConsumingValues) {
    static_assert(rstd::Impled<BTreeMap<String, String>, rstd::fmt::Debug>);
    auto values = BTreeMap<String, String>::make();
    EXPECT_EQ(rstd::format("{:?}", values), "{}"_str);
    values.insert("z"_Str, "last"_Str);
    values.insert("a"_Str, "first"_Str);
    auto settings = Settings { rstd::move(values) };
    EXPECT_EQ(rstd::format("{:?}", settings),
              "Settings { values: {\"a\": \"first\", \"z\": \"last\"} }"_str);
    EXPECT_EQ(
        rstd::format("{:#?}", settings),
        "Settings {\n    values: {\n        \"a\": \"first\",\n        \"z\": \"last\",\n    },\n}"_str);
    EXPECT_EQ(settings.values.len(), rstd::usize(2));

    auto nested = BTreeMap<String, Leaf>::make();
    nested.insert("key"_Str, Leaf { "nested"_Str });
    EXPECT_EQ(rstd::format("{:#?}", nested),
              "{\n    \"key\": Leaf {\n        message: \"nested\",\n    },\n}"_str);
    rstd::size_t remaining = 10;
    auto         limited   = [](void* state, const rstd::uint8_t*, rstd::size_t length) -> bool {
        auto& budget = *static_cast<rstd::size_t*>(state);
        if (length > budget) return false;
        budget -= length;
        return true;
    };
    rstd::fmt::Formatter formatter(&remaining, limited);
    EXPECT_FALSE(rstd::as<rstd::fmt::Debug>(settings.values).fmt(formatter));
}

TEST(DeriveDebug, SlicesUseListDebugForBytesRecordsAndMutableBorrows) {
    static_assert(rstd::Impled<rstd::slice<rstd::u8>, rstd::fmt::Debug>);
    static_assert(rstd::Impled<rstd::slice<DebugHeader>, rstd::fmt::Debug>);
    rstd::slice<rstd::u8> empty;
    EXPECT_EQ(rstd::format("{:?}", empty), "[]"_str);
    auto bytes = "AZ"_str.as_bytes();
    EXPECT_EQ(rstd::format("{:?}", bytes), "[65, 90]"_str);
    auto headers = Vec<DebugHeader>::make();
    headers.push({ "name"_Str, bytes });
    auto fields = headers.as_slice();
    EXPECT_EQ(rstd::format("{:?}", fields),
              "[DebugHeader { name: \"name\", value: [65, 90] }]"_str);
    EXPECT_EQ(
        rstd::format("{:#?}", fields),
        "[\n    DebugHeader {\n        name: \"name\",\n        value: [\n            65,\n            90,\n        ],\n    },\n]"_str);
    auto numbers = Vec<rstd::i32>::make();
    numbers.push(rstd::i32(1));
    numbers.push(rstd::i32(2));
    auto mutable_fields = numbers.deref_mut();
    EXPECT_EQ(rstd::format("{:?}", mutable_fields), "[1, 2]"_str);
    EXPECT_EQ(numbers[rstd::usize()], rstd::i32(1));
    EXPECT_EQ(headers.len(), rstd::usize(1));
}
} // namespace derive_tests::errors
