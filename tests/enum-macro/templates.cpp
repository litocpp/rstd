#include <rstd/test/gtest.hpp>
#include <type_traits>

import rstd.tests.enum_macro.fixture;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using alloc::sync::Arc;
using namespace enum_macro::fixture;

namespace enum_macro::templates
{
[[pmacro::invoke("rstd::Enum",
                 template<class T>
                 [[derive(Debug, Clone)]] enum Tree {
                     Leaf(T),
                     Branch(Arc<Tree<T>>),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 template<class T>
                 enum DefaultPayload {
                     Empty,
                     [[default]] Value(T),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 enum ComplexTypes {
                     Nested(rstd::tuple<i32, rstd::tuple<i32, i32>>),
                     Function(int (*)(int)),
                 })]];

static_assert(! std::is_same_v<Maybe<i32>, Maybe<String>>);
static_assert(std::is_same_v<decltype(rstd::declval<Maybe<i32>&>().as_Some().value), i32>);
static_assert(
    std::is_same_v<decltype(rstd::declval<const Maybe<i32>::Some_payload&>().template get<0>()),
                   const i32&>);

TEST(EnumMacroTemplate, InstantiatesTypeParametersWithoutChangingNamedAccessors) {
    auto number = Maybe<i32>::Some(i32(7));
    auto text   = Maybe<String>::Some("owned"_Str);
    EXPECT_EQ(number.as_Some().value, i32(7));
    EXPECT_EQ(text.as_Some().value.as_str(), "owned"_str);
    EXPECT_EQ(rstd::format("{:?}", number), "Some(7)"_str);
    auto copy            = rstd::as<rstd::clone::Clone>(text).clone();
    copy.as_Some().value = "changed"_Str;
    EXPECT_EQ(text.as_Some().value.as_str(), "owned"_str);
}

TEST(EnumMacroTemplate, AcceptsSeveralNamedTypeParametersAndDependentDefaults) {
    auto pair = Pair<i32, String>::Value(i32(8), "pair"_Str);
    EXPECT_EQ(pair.as_Value().field_0, i32(8));
    EXPECT_EQ(pair.as_Value().field_1.as_str(), "pair"_str);
    Pair<String, i32> empty;
    EXPECT_TRUE(empty.is_Empty());
    DefaultPayload<i32> default_value;
    EXPECT_TRUE(default_value.is_Value());
    EXPECT_EQ(default_value.as_Value().field_0, i32());
}

TEST(EnumMacroTemplate, DefersRecursivePayloadTraitsUntilSpecializationIsComplete) {
    auto leaf    = Arc<Tree<String>>::make(Tree<String>::Leaf("root"_Str));
    auto address = &*leaf;
    auto tree    = Tree<String>::Branch(rstd::move(leaf));
    EXPECT_EQ(rstd::format("{:?}", tree), "Branch(Leaf(\"root\"))"_str);
    auto copy = rstd::as<rstd::clone::Clone>(tree).clone();
    EXPECT_EQ(&*copy.as_Branch().field_0, address);
    EXPECT_EQ(copy.as_Branch().field_0->as_Leaf().field_0.as_str(), "root"_str);
}

TEST(EnumMacroTemplate, PreservesNestedTypeCommasAndFunctionPointerTypeIds) {
    auto value = ComplexTypes::Nested(
        rstd::tuple<i32, rstd::tuple<i32, i32>> { i32(1), { i32(2), i32(3) } });
    EXPECT_EQ(value.as_Nested().field_0.template get<1>().template get<1>(), i32(3));
    auto function = ComplexTypes::Function(+[](int value) {
        return value + 2;
    });
    EXPECT_EQ(function.as_Function().field_0(5), 7);
}
} // namespace enum_macro::templates
