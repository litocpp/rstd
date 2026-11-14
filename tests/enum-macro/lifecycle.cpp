#include <rstd/test/gtest.hpp>
#include <type_traits>

import rstd.core;

namespace enum_macro::lifecycle
{
struct Tracked {
    static inline int live         = 0;
    static inline int copies       = 0;
    static inline int moves        = 0;
    static inline int copy_assigns = 0;
    static inline int move_assigns = 0;

    int value;

    explicit Tracked(int value) noexcept: value(value) { ++live; }
    Tracked(const Tracked& other) noexcept: value(other.value) {
        ++live;
        ++copies;
    }
    Tracked(Tracked&& other) noexcept: value(other.value) {
        ++live;
        ++moves;
    }
    auto operator=(const Tracked& other) noexcept -> Tracked& {
        value = other.value;
        ++copy_assigns;
        return *this;
    }
    auto operator=(Tracked&& other) noexcept -> Tracked& {
        value = other.value;
        ++move_assigns;
        return *this;
    }
    ~Tracked() { --live; }

    static void reset() noexcept { copies = moves = copy_assigns = move_assigns = 0; }
};

struct MoveOnly {
    int value;
    explicit MoveOnly(int value) noexcept: value(value) {}
    MoveOnly(const MoveOnly&)                        = delete;
    MoveOnly(MoveOnly&&) noexcept                    = default;
    auto operator=(const MoveOnly&) -> MoveOnly&     = delete;
    auto operator=(MoveOnly&&) noexcept -> MoveOnly& = default;
};

struct Immobile {
    int value;
    explicit Immobile(int value) noexcept: value(value) {}
    Immobile(const Immobile&)                    = delete;
    Immobile(Immobile&&)                         = delete;
    auto operator=(const Immobile&) -> Immobile& = delete;
    auto operator=(Immobile&&) -> Immobile&      = delete;
};

struct FallibleArgument {
    int value;
};

struct Staged {
    static inline int live                     = 0;
    static inline int live_during_construction = -1;
    int               value;

    explicit Staged(FallibleArgument value): value(value.value) {
        live_during_construction = live;
        ++live;
    }
    Staged(const Staged&) = delete;
    Staged(Staged&& other) noexcept: value(other.value) { ++live; }
    auto operator=(const Staged&) -> Staged& = delete;
    auto operator=(Staged&& other) noexcept -> Staged& {
        value = other.value;
        return *this;
    }
    ~Staged() { --live; }
};

[[pmacro::invoke("rstd::Enum",
                 enum TrackedEnum {
                     Empty,
                     Tuple(Tracked),
                     External = Tracked,
                     Number(int),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 enum MoveEnum {
                     Empty,
                     Value(MoveOnly),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 enum DefaultTuple {
                     [[default]] Value(MoveOnly),
                     Empty,
                 })]];

[[pmacro::invoke("rstd::Enum",
                 enum DefaultRecord {
                     [[default]] Value { item: MoveOnly },
                     Empty,
                 })]];

[[pmacro::invoke("rstd::Enum",
                 enum DefaultExternal {
                     [[default]] Value = MoveOnly,
                     Empty,
                 })]];

static_assert(! std::is_default_constructible_v<DefaultTuple>);
static_assert(! std::is_default_constructible_v<DefaultRecord>);
static_assert(! std::is_default_constructible_v<DefaultExternal>);

[[pmacro::invoke("rstd::Enum",
                 enum ImmobileEnum {
                     Empty,
                     Value = Immobile,
                 })]];

[[pmacro::invoke("rstd::Enum",
                 enum StagedEnum {
                     Value = Staged,
                 })]];

[[pmacro::invoke("rstd::Enum",
                 enum Number {
                     [[default]] Empty,
                     Value(int),
                 })]];

struct NoexceptVisitor {
    template<auto Tag, typename... Payload>
    auto operator()(rstd::choice_tag<Tag>, Payload&&...) const noexcept -> int {
        return 7;
    }
};

static_assert(! std::is_copy_constructible_v<MoveEnum>);
static_assert(! std::is_copy_assignable_v<MoveEnum>);
static_assert(std::is_nothrow_move_constructible_v<MoveEnum>);
static_assert(std::is_nothrow_move_assignable_v<MoveEnum>);
static_assert(! std::is_move_constructible_v<ImmobileEnum>);
static_assert(! std::is_copy_constructible_v<ImmobileEnum>);
static_assert(std::is_trivially_copy_constructible_v<Number>);
static_assert(std::is_trivially_move_constructible_v<Number>);
static_assert(std::is_trivially_destructible_v<Number>);
static_assert(! std::is_trivially_destructible_v<TrackedEnum>);
static_assert(noexcept(Number::Value(1)));
static_assert(noexcept(Number::Empty()));
static_assert(noexcept(rstd::declval<Number&>().replace_Value(1)));
static_assert(noexcept(rstd::declval<Number&>().visit(NoexceptVisitor {})));
static_assert(! noexcept(StagedEnum::Value(FallibleArgument { 1 })));
static_assert(! noexcept(rstd::declval<StagedEnum&>().replace_Value(FallibleArgument { 1 })));

static_assert(
    std::is_same_v<decltype(rstd::declval<Number&>().as_Value()), Number::Value_payload&>);
static_assert(std::is_same_v<decltype(rstd::declval<const Number&>().as_Value()),
                             const Number::Value_payload&>);
static_assert(
    std::is_same_v<decltype(rstd::declval<Number&&>().as_Value()), Number::Value_payload&&>);
static_assert(std::is_same_v<decltype(rstd::declval<const Number&&>().as_Value()),
                             const Number::Value_payload&&>);
static_assert(
    std::is_same_v<decltype(rstd::declval<Number::Value_payload&>().template get<0>()), int&>);
static_assert(
    std::is_same_v<decltype(rstd::declval<const Number::Value_payload&>().template get<0>()),
                   const int&>);
static_assert(
    std::is_same_v<decltype(rstd::declval<Number::Value_payload&&>().template get<0>()), int&&>);
static_assert(
    std::is_same_v<decltype(rstd::declval<const Number::Value_payload&&>().template get<0>()),
                   const int&&>);

constexpr auto constexpr_value = [] {
    Number value;
    value.replace_Value(9);
    auto other = value;
    other.as_Value().template get<0>() += 3;
    value.replace_Empty();
    return value.is_Empty() && other.as_Value().field_0 == 12;
}();
static_assert(constexpr_value);

TEST(EnumMacroLifecycle, CopiesMovesAndDestroysOnlyActivePayloads) {
    ASSERT_EQ(Tracked::live, 0);
    {
        auto original = TrackedEnum::Tuple(Tracked { 4 });
        ASSERT_EQ(Tracked::live, 1);
        Tracked::reset();
        auto copied = original;
        EXPECT_EQ(Tracked::live, 2);
        EXPECT_EQ(Tracked::copies, 1);
        auto moved = rstd::move(copied);
        EXPECT_EQ(Tracked::live, 3);
        EXPECT_EQ(Tracked::moves, 1);
        EXPECT_EQ(moved.as_Tuple().field_0.value, 4);
        moved.replace_Number(8);
        EXPECT_EQ(Tracked::live, 2);
        EXPECT_EQ(moved.as_Number().field_0, 8);
    }
    EXPECT_EQ(Tracked::live, 0);
}

TEST(EnumMacroLifecycle, AssignsWithinAndAcrossVariantsAndHandlesSelfAssignment) {
    ASSERT_EQ(Tracked::live, 0);
    {
        auto source = TrackedEnum::External(3);
        auto same   = TrackedEnum::External(4);
        auto other  = TrackedEnum::Empty();
        Tracked::reset();
        same = source;
        EXPECT_EQ(Tracked::copy_assigns, 1);
        other = source;
        EXPECT_EQ(Tracked::copies, 1);
        EXPECT_EQ(other.as_External().value, 3);
        same = rstd::move(source);
        EXPECT_EQ(Tracked::move_assigns, 1);
        auto* alias = &same;
        same        = *alias;
        same        = rstd::move(*alias);
        EXPECT_TRUE(same.is_External());
    }
    EXPECT_EQ(Tracked::live, 0);
}

TEST(EnumMacroLifecycle, SupportsImmobileExternalPayloadInPlace) {
    auto value = ImmobileEnum::Value(9);
    EXPECT_EQ(value.as_Value().value, 9);
}

TEST(EnumMacroLifecycle, NonDefaultPayloadDoesNotPreventDeclaringTheEnumOrUsingFactories) {
    auto tuple = DefaultTuple::Value(MoveOnly { 3 });
    EXPECT_EQ(tuple.as_Value().field_0.value, 3);
    EXPECT_TRUE(DefaultTuple::Empty().is_Empty());
    auto record = DefaultRecord::Value(MoveOnly { 4 });
    EXPECT_EQ(record.as_Value().item.value, 4);
    EXPECT_TRUE(DefaultRecord::Empty().is_Empty());
    auto external = DefaultExternal::Value(5);
    EXPECT_EQ(external.as_Value().value, 5);
    EXPECT_TRUE(DefaultExternal::Empty().is_Empty());
}

TEST(EnumMacroLifecycle, ConstructsReplacementBeforeDroppingFallibleActiveValue) {
    ASSERT_EQ(Staged::live, 0);
    {
        auto value                       = StagedEnum::Value(FallibleArgument { 1 });
        Staged::live_during_construction = -1;
        value.replace_Value(FallibleArgument { 2 });
        EXPECT_EQ(Staged::live_during_construction, 1);
        EXPECT_EQ(Staged::live, 1);
        EXPECT_EQ(value.as_Value().value, 2);
    }
    EXPECT_EQ(Staged::live, 0);
}

TEST(EnumMacroLifecycle, VisitsAndMutatesTheSelectedPayload) {
    auto value   = Number::Value(5);
    auto visitor = []<auto Tag>(rstd::choice_tag<Tag>, auto&&... payload) -> int {
        if constexpr (Tag == Number::Tag::Empty) {
            return 0;
        } else {
            ((payload.field_0 += 2), ...);
            return (payload.field_0 + ...);
        }
    };
    EXPECT_EQ(value.visit_mut(visitor), 7);
    EXPECT_EQ(value.as_Value().field_0, 7);
    EXPECT_EQ(value.visit(NoexceptVisitor {}), 7);
    value.replace_Empty();
    EXPECT_EQ(value.visit_mut(visitor), 0);
}

TEST(EnumMacroLifecycle, RejectsAccessToAnInactiveVariant) {
    auto value = Number::Empty();
    EXPECT_DEATH((void)value.as_Value(), "");
}
} // namespace enum_macro::lifecycle

namespace enum_macro::lifecycle
{
struct CloneCounted {
    int value;
};
} // namespace enum_macro::lifecycle

namespace rstd
{
template<>
struct Impl<clone::Clone, enum_macro::lifecycle::CloneCounted>
    : DefaultInImpl<clone::Clone, enum_macro::lifecycle::CloneCounted> {
    auto clone() const -> enum_macro::lifecycle::CloneCounted { return { this->self().value + 1 }; }
};
} // namespace rstd

namespace enum_macro::lifecycle
{
[[pmacro::invoke("rstd::Enum",
                 [[derive(Clone)]] enum CloneEnum {
                     Tuple(CloneCounted),
                     External = CloneCounted,
                 })]];

TEST(EnumMacroLifecycle, CloneCallsPayloadTraitsEvenWhenCppCopyIsAvailable) {
    const auto value  = CloneEnum::Tuple(CloneCounted { 3 });
    auto       copied = value;
    EXPECT_EQ(copied.as_Tuple().field_0.value, 3);
    auto cloned = rstd::as<rstd::clone::Clone>(value).clone();
    EXPECT_EQ(cloned.as_Tuple().field_0.value, 4);
    const auto external = CloneEnum::External(CloneCounted { 7 });
    rstd::as<rstd::clone::Clone>(cloned).clone_from(external);
    ASSERT_TRUE(cloned.is_External());
    EXPECT_EQ(cloned.as_External().value, 8);
}
} // namespace enum_macro::lifecycle
