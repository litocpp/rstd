#include <rstd/enum.hpp>
#include <rstd/test/gtest.hpp>

import rstd.core;
import rstd.alloc;

using alloc::string::String;
using alloc::vec::Vec;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace derive_tests::clones
{
struct Counted {
    int value;
};
} // namespace derive_tests::clones

namespace rstd
{
template<>
struct Impl<clone::Clone, derive_tests::clones::Counted>
    : DefaultInImpl<clone::Clone, derive_tests::clones::Counted> {
    auto clone() const -> derive_tests::clones::Counted { return { this->self().value + 1 }; }
};
} // namespace rstd

namespace derive_tests::clones
{
struct [[pmacro::derive("rstd::Clone")]] Copyable {
    Counted field;
};

struct [[pmacro::derive("rstd::Clone")]] Leaf {
    String text;
};

struct [[pmacro::derive("rstd::Clone")]] Branch {
    Vec<Leaf>    children;
    Option<Leaf> selected;
};

struct [[pmacro::derive("rstd::Clone")]] Empty {};

class [[pmacro::derive("rstd::Clone")]] Private {
    friend constexpr auto rstd_clone_definition(Private*);

    String       text_;
    Vec<Counted> values_;

    Private(String text, Vec<Counted> values)
        : text_(rstd::move(text)), values_(rstd::move(values)) {}

public:
    static auto make() -> Private {
        Vec<Counted> values;
        values.push({ 4 });
        return Private("private"_Str, rstd::move(values));
    }
    auto text() const -> ref<str> { return text_.as_str(); }
    auto value() const -> int { return values_[usize()].value; }
};

class [[pmacro::derive("rstd::Clone"), pmacro::helper("rstd::Clone", variants)]] Event {
    RSTD_ENUM_TYPES(Event, (Idle), (Text, (Leaf)), (OtherText, (Leaf)))
};

enum class [[pmacro::derive("rstd::Clone")]] Code
{
    Value = 3
};

static_assert(rstd::Impled<Branch, rstd::clone::Clone>);
static_assert(rstd::Impled<Private, rstd::clone::Clone>);
static_assert(rstd::Impled<Event, rstd::clone::Clone>);
static_assert(! rstd::mtp::copy<Branch>);
static_assert(! rstd::mtp::copy<Private>);
static_assert(! rstd::mtp::copy<Event>);
static_assert(! rstd::Impled<Branch, rstd::Copy>);
static_assert(rstd::mtp::copy<Copyable>);

TEST(DeriveClone, CopyableRecordsStillCallFieldClone) {
    const auto source = Copyable { { 3 } };
    auto       copy   = rstd::as<rstd::clone::Clone>(source).clone();
    EXPECT_EQ(copy.field.value, 4);
    EXPECT_EQ(source.field.value, 3);
    rstd::as<rstd::clone::Clone>(copy).clone_from(source);
    EXPECT_EQ(copy.field.value, 4);
    rstd::as<rstd::clone::Clone>(copy).clone_from(copy);
    EXPECT_EQ(copy.field.value, 5);
}

TEST(DeriveClone, NestedOptionAndVectorOwnIndependentValues) {
    Branch source;
    source.children.push({ "child"_Str });
    source.selected             = Some(Leaf { "selected"_Str });
    auto copy                   = rstd::as<rstd::clone::Clone>(source).clone();
    copy.children[usize()].text = "changed"_Str;
    copy.selected->text         = "changed"_Str;
    EXPECT_EQ(source.children[usize()].text.as_str(), "child"_str);
    EXPECT_EQ(source.selected->text.as_str(), "selected"_str);

    rstd::as<rstd::clone::Clone>(copy).clone_from(source);
    EXPECT_EQ(copy.children[usize()].text.as_str(), "child"_str);
    EXPECT_EQ(copy.selected->text.as_str(), "selected"_str);
    rstd::as<rstd::clone::Clone>(copy).clone_from(copy);
    EXPECT_EQ(copy.selected->text.as_str(), "selected"_str);
    Branch empty;
    rstd::as<rstd::clone::Clone>(copy).clone_from(empty);
    EXPECT_TRUE(copy.children.is_empty());
    EXPECT_TRUE(copy.selected.is_none());
    const auto empty_record = Empty {};
    auto       unit         = rstd::as<rstd::clone::Clone>(empty_record).clone();
    rstd::as<rstd::clone::Clone>(unit).clone_from(unit);
}

TEST(DeriveClone, FriendDefinitionAccessesPrivateFieldsAndConstructor) {
    auto source = Private::make();
    auto copy   = rstd::as<rstd::clone::Clone>(source).clone();
    EXPECT_EQ(copy.text(), "private"_str);
    EXPECT_EQ(copy.value(), 5);
    EXPECT_EQ(source.value(), 4);
    rstd::as<rstd::clone::Clone>(copy).clone_from(source);
    EXPECT_EQ(copy.value(), 5);
}

TEST(DeriveClone, VariantsKeepActiveTagAndClonePayload) {
    auto source = Event::OtherText(Leaf { "payload"_Str });
    auto copy   = rstd::as<rstd::clone::Clone>(source).clone();
    ASSERT_TRUE(copy.is_OtherText());
    copy.as_OtherText().text = "changed"_Str;
    EXPECT_EQ(source.as_OtherText().text.as_str(), "payload"_str);
    auto text = Event::Text(Leaf { "text"_Str });
    rstd::as<rstd::clone::Clone>(copy).clone_from(text);
    ASSERT_TRUE(copy.is_Text());
    EXPECT_EQ(copy.as_Text().text.as_str(), "text"_str);
    rstd::as<rstd::clone::Clone>(copy).clone_from(Event::Idle());
    EXPECT_TRUE(copy.is_Idle());
    const auto code = Code::Value;
    EXPECT_EQ(rstd::as<rstd::clone::Clone>(code).clone(), Code::Value);
}
} // namespace derive_tests::clones
