#include <gtest/gtest.h>
#include <concepts>
#include <memory>
#include <string>
#include <utility>

import rstd.core;

#include <rstd/enum.hpp>

namespace
{

#define MESSAGE_VARIANTS(V)  \
    V(Quit, ())              \
    V(Move, (int x; int y;)) \
    V(Write, (std::string text;))

RSTD_ENUM(Message, MESSAGE_VARIANTS)

RSTD_ENUM_WITH_DEFAULT(DefaultMessage, MESSAGE_VARIANTS, Quit)

#define BOX_VARIANTS(V) \
    V(Empty, ())        \
    V(Value, (std::unique_ptr<int> value;))

RSTD_ENUM(Box, BOX_VARIANTS)

#define MAYBE_VARIANTS(V) \
    V(None, ())           \
    V(Some, (T value;))

RSTD_ENUM_TEMPLATE((class T), Maybe, MAYBE_VARIANTS)

RSTD_ENUM_TEMPLATE_WITH_DEFAULT((class T), DefaultMaybe, MAYBE_VARIANTS, None)

#define COLOR_VARIANTS(V) \
    V(Red)                \
    V(Green)              \
    V(Blue)

RSTD_TAG_ENUM(Color, COLOR_VARIANTS)

RSTD_TAG_ENUM_WITH_DEFAULT(DefaultColor, COLOR_VARIANTS, Green)

#define DEFAULT_POINTS_VARIANTS(V) \
    V(Idle, ())                    \
    V(Points, (int value;))

RSTD_ENUM_WITH_DEFAULT(DefaultPoints, DEFAULT_POINTS_VARIANTS, Points, 5)

#define INLINE_SCORE_VARIANTS(V) \
    V(Idle, ())                  \
    V(Points, (int value;))

class InlineScore {
    RSTD_ENUM_BODY_WITH_DEFAULT(InlineScore, INLINE_SCORE_VARIANTS, Idle)

private:
    int multiplier_ = 2;

public:
    constexpr void set_multiplier(int value) noexcept { multiplier_ = value; }

    [[nodiscard]]
    constexpr auto score() const noexcept -> int {
        if (is_Points()) {
            return as_Points().value * multiplier_;
        }
        return 0;
    }
};

struct FallibleArg {
    int value;
};

struct NoThrowMovePayload {
    static inline int live_payloads               = 0;
    static inline int live_payloads_during_create = -1;

    int value;

    explicit NoThrowMovePayload(FallibleArg arg): value(arg.value) {
        live_payloads_during_create = live_payloads;
        ++live_payloads;
    }
    NoThrowMovePayload(const NoThrowMovePayload&)            = delete;
    NoThrowMovePayload& operator=(const NoThrowMovePayload&) = delete;
    NoThrowMovePayload(NoThrowMovePayload&& other) noexcept: value(other.value) { ++live_payloads; }
    NoThrowMovePayload& operator=(NoThrowMovePayload&& other) noexcept {
        value = other.value;
        return *this;
    }
    ~NoThrowMovePayload() { --live_payloads; }
};

auto score(const Message& message) -> int {
    int result = -1;

    RSTD_MATCH(message) {
        RSTD_CASE(Quit) { result = 0; }
        RSTD_CASE(Move, x, y) { result = x + y; }
        RSTD_CASE(Write, text) { result = static_cast<int>(text.size()); }
    }

    return result;
}

auto color_score(const Color& color) -> int {
    int result = -1;

    RSTD_MATCH(color) {
        RSTD_CASE(Red) { result = 1; }
        RSTD_CASE(Green) { result = 2; }
        RSTD_CASE(Blue) { result = 3; }
    }

    return result;
}

} // namespace

TEST(Enum, ConstructsAndReadsPayload) {
    auto message = Message::Move(3, 4);

    EXPECT_TRUE(message.is_Move());
    EXPECT_FALSE(message.is_Quit());
    EXPECT_EQ(message.tag(), Message::Tag::Move);
    EXPECT_EQ(message.index(), 1u);
    EXPECT_EQ(message.as_Move().x, 3);
    EXPECT_EQ(message.as_Move().y, 4);
}

TEST(Enum, MatchesByReference) {
    EXPECT_EQ(score(Message::Quit()), 0);
    EXPECT_EQ(score(Message::Move(5, 7)), 12);
    EXPECT_EQ(score(Message::Write("hello")), 5);
}

TEST(Enum, MatchesWholePayload) {
    auto message = Message::Write("payload");
    int  result  = 0;

    RSTD_MATCH(message) {
        RSTD_CASE(Quit) { result = 0; }
        RSTD_CASE(Move, x, y) { result = x + y; }
        RSTD_CASE_PAYLOAD(Write, payload) { result = static_cast<int>(payload.text.size()); }
    }

    EXPECT_EQ(result, 7);
}

TEST(Enum, ReplacesVariantThroughGeneratedInterface) {
    auto message = Message::Quit();

    message.replace_Move(2, 5);
    ASSERT_TRUE(message.is_Move());
    EXPECT_EQ(message.as_Move().x, 2);
    EXPECT_EQ(message.as_Move().y, 5);

    message.replace_Write("done");
    ASSERT_TRUE(message.is_Write());
    EXPECT_EQ(message.as_Write().text, "done");
}

TEST(Enum, SupportsMoveOnlyPayload) {
    static_assert(! std::copy_constructible<Box>);
    static_assert(std::move_constructible<Box>);

    auto                 box = Box::Value(std::make_unique<int>(42));
    std::unique_ptr<int> taken;

    RSTD_MATCH(std::move(box)) {
        RSTD_CASE(Empty) {}
        RSTD_CASE(Value, value) { taken = std::move(value); }
    }

    ASSERT_NE(taken, nullptr);
    EXPECT_EQ(*taken, 42);
}

TEST(Enum, SupportsTemplateEnum) {
    auto value = Maybe<int>::Some(9);
    auto none  = Maybe<int>::None();

    EXPECT_TRUE(value.is_Some());
    EXPECT_TRUE(none.is_None());
    EXPECT_EQ(value.as_Some().value, 9);
}

TEST(Enum, SupportsDefaultVariant) {
    static_assert(std::default_initializable<DefaultMessage>);
    static_assert(std::default_initializable<DefaultMaybe<int>>);
    static_assert(std::default_initializable<DefaultColor>);
    static_assert(std::default_initializable<DefaultPoints>);

    DefaultMessage message;
    EXPECT_TRUE(message.is_Quit());
    EXPECT_EQ(message.tag(), DefaultMessage::Tag::Quit);

    DefaultMaybe<int> maybe;
    EXPECT_TRUE(maybe.is_None());

    DefaultColor color;
    EXPECT_TRUE(color.is_Green());
    EXPECT_EQ(color.tag(), DefaultColor::Tag::Green);

    DefaultPoints points;
    EXPECT_TRUE(points.is_Points());
    EXPECT_EQ(points.as_Points().value, 5);
}

TEST(Enum, SupportsTagOnlyEnum) {
    static_assert(sizeof(Color) == sizeof(rstd::u8));

    auto color = Color::Red();
    EXPECT_TRUE(color.is_Red());
    EXPECT_FALSE(color.is_Green());
    EXPECT_EQ(color.tag(), Color::Tag::Red);
    EXPECT_EQ(color.index(), 0u);
    EXPECT_EQ(color_score(color), 1);

    color.replace_Blue();
    EXPECT_TRUE(color.is_Blue());
    EXPECT_EQ(color.tag(), Color::Tag::Blue);
    EXPECT_EQ(color.index(), 2u);
    EXPECT_EQ(color_score(color), 3);
}

TEST(Enum, SupportsCustomClassWithInlineParts) {
    InlineScore idle;
    auto score = InlineScore::Points(6);

    EXPECT_TRUE(idle.is_Idle());
    EXPECT_EQ(idle.score(), 0);

    EXPECT_TRUE(score.is_Points());
    EXPECT_EQ(score.score(), 12);

    score.set_multiplier(3);
    EXPECT_EQ(score.score(), 18);

    score.replace_Idle();
    EXPECT_TRUE(score.is_Idle());
    EXPECT_EQ(score.score(), 0);
}

TEST(Enum, ReplaceBuildsTemporaryBeforeDestroyingCurrentPayload) {
    static_assert(! rstd::mtp::noex_init<NoThrowMovePayload, FallibleArg>);
    static_assert(rstd::mtp::noex_move<NoThrowMovePayload>);

    {
        auto storage = rstd::enum_detail::storage<NoThrowMovePayload>(
            rstd::enum_detail::in_place_index<0>, FallibleArg { 1 });

        NoThrowMovePayload::live_payloads_during_create = -1;
        storage.replace(rstd::enum_detail::in_place_index<0>, FallibleArg { 2 });

        EXPECT_TRUE(storage.is(rstd::enum_detail::in_place_index<0>));
        EXPECT_EQ(storage.get(rstd::enum_detail::in_place_index<0>).value, 2);
        EXPECT_EQ(NoThrowMovePayload::live_payloads_during_create, 1);
        EXPECT_EQ(NoThrowMovePayload::live_payloads, 1);
    }

    EXPECT_EQ(NoThrowMovePayload::live_payloads, 0);
}
