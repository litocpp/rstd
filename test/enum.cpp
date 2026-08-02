#include <rstd/test/gtest.hpp>
#include <concepts>
#include <memory>
#include <string>
#include <utility>

import rstd.core;

#include <rstd/enum.hpp>

namespace
{

class Message final {
    RSTD_ENUM(Message, (Quit), (Move, (int x; int y;)), (Write, (std::string text;)))
};

class DefaultMessage final {
    RSTD_ENUM_DEFAULT(DefaultMessage,
                      (Quit),
                      (Quit),
                      (Move, (int x; int y;)),
                      (Write, (std::string text;)))
};

class Box final {
    RSTD_ENUM(Box, (Empty), (Value, (std::unique_ptr<int> value;)))
};

template<class T>
class Maybe final {
    RSTD_ENUM(Maybe, (None), (Some, (T value;)))
};

template<class T>
class DefaultMaybe final {
    RSTD_ENUM_DEFAULT(DefaultMaybe, (None), (None), (Some, (T value;)))
};

class Color final {
    RSTD_ENUM(Color, (Red), (Green), (Blue))
};

class DefaultColor final {
    RSTD_ENUM_DEFAULT(DefaultColor, (Green), (Red), (Green), (Blue))
};

class DefaultPoints final {
    RSTD_ENUM_DEFAULT(DefaultPoints, (Points, 5), (Idle), (Points, (int value;)))
};

class InlineScore {
    RSTD_ENUM_DEFAULT(InlineScore, (Idle), (Idle), (Points, (int value;)))

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

struct EnumBase {
    int base_value = 4;
};

class ExtendedMessage final : public EnumBase {
    RSTD_ENUM(ExtendedMessage, (Idle), (Value, (int value;)))

public:
    [[nodiscard]]
    constexpr auto total() const noexcept -> int {
        return base_value + (is_Value() ? as_Value().value : 0);
    }
};

class OverloadedFactory final {
    RSTD_ENUM(OverloadedFactory, (Empty), (Value, (int value;)))

public:
    [[nodiscard]]
    static constexpr auto Value(char value) noexcept -> Self {
        return Self::Value(static_cast<int>(value));
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

struct TrackedPayload {
    static inline int live         = 0;
    static inline int copies       = 0;
    static inline int moves        = 0;
    static inline int copy_assigns = 0;
    static inline int move_assigns = 0;

    int value;

    explicit TrackedPayload(int value) noexcept: value(value) { ++live; }
    TrackedPayload(TrackedPayload const& other) noexcept: value(other.value) {
        ++live;
        ++copies;
    }
    TrackedPayload(TrackedPayload&& other) noexcept: value(other.value) {
        ++live;
        ++moves;
    }
    auto operator=(TrackedPayload const& other) noexcept -> TrackedPayload& {
        value = other.value;
        ++copy_assigns;
        return *this;
    }
    auto operator=(TrackedPayload&& other) noexcept -> TrackedPayload& {
        value = other.value;
        ++move_assigns;
        return *this;
    }
    ~TrackedPayload() { --live; }
};

struct CopyOnlyPayload {
    int value;

    explicit CopyOnlyPayload(int value) noexcept: value(value) {}
    CopyOnlyPayload(CopyOnlyPayload const&) noexcept                    = default;
    CopyOnlyPayload(CopyOnlyPayload&&)                                  = delete;
    auto operator=(CopyOnlyPayload const&) noexcept -> CopyOnlyPayload& = default;
    auto operator=(CopyOnlyPayload&&) -> CopyOnlyPayload&               = delete;
};

struct MoveOnlyPayload {
    int value;

    explicit MoveOnlyPayload(int value) noexcept: value(value) {}
    MoveOnlyPayload(MoveOnlyPayload const&)                        = delete;
    MoveOnlyPayload(MoveOnlyPayload&&) noexcept                    = default;
    auto operator=(MoveOnlyPayload const&) -> MoveOnlyPayload&     = delete;
    auto operator=(MoveOnlyPayload&&) noexcept -> MoveOnlyPayload& = default;
};

struct ImmobilePayload {
    int value;

    explicit ImmobilePayload(int value) noexcept: value(value) {}
    ImmobilePayload(ImmobilePayload const&)                    = delete;
    ImmobilePayload(ImmobilePayload&&)                         = delete;
    auto operator=(ImmobilePayload const&) -> ImmobilePayload& = delete;
    auto operator=(ImmobilePayload&&) -> ImmobilePayload&      = delete;
};

struct NonTrivialDropPayload {
    ~NonTrivialDropPayload() {}
};

enum class ChoiceTag : int
{
    Empty  = -4,
    Number = 7,
    Pair   = 31,
};

using DirectChoice = rstd::Choice<RSTD_CHOICE_TYPES((ChoiceTag::Empty, void),
                                                    (ChoiceTag::Number, int),
                                                    (ChoiceTag::Pair, int, std::string))>;

using CommaTypeChoice = rstd::Choice<RSTD_CHOICE_TYPES((ChoiceTag::Pair, rstd::tuple<int, int>))>;

auto score(const Message& message) -> int {
    int result = -1;

    RSTD_MATCH(message) {
        RSTD_CASE(Quit) {
            result = 0;
        }
        RSTD_CASE(Move, x, y) {
            result = x + y;
        }
        RSTD_CASE(Write, text) {
            result = static_cast<int>(text.size());
        }
    }

    return result;
}

auto color_score(const Color& color) -> int {
    int result = -1;

    RSTD_MATCH(color) {
        RSTD_CASE(Red) {
            result = 1;
        }
        RSTD_CASE(Green) {
            result = 2;
        }
        RSTD_CASE(Blue) {
            result = 3;
        }
    }

    return result;
}

} // namespace

TEST(Choice, MapsArbitraryTagsAndPayloadShapes) {
    static_assert(std::same_as<DirectChoice::Tag, ChoiceTag>);
    static_assert(std::same_as<DirectChoice::TypeForTag<ChoiceTag::Empty>, void>);
    static_assert(std::same_as<DirectChoice::TypeForTag<ChoiceTag::Number>, int>);
    static_assert(
        std::same_as<DirectChoice::TypeForTag<ChoiceTag::Pair>, rstd::tuple<int, std::string>>);

    auto choice = DirectChoice::with<ChoiceTag::Number>(3);
    EXPECT_EQ(choice.which(), ChoiceTag::Number);
    EXPECT_EQ(choice.index(), 1u);
    EXPECT_EQ(choice.as<ChoiceTag::Number>(), 3);

    choice.set<ChoiceTag::Pair>(4, "value");
    EXPECT_EQ(choice.which(), ChoiceTag::Pair);
    EXPECT_EQ(choice.index(), 2u);
    EXPECT_EQ(choice.as<ChoiceTag::Pair>().template get<0>(), 4);
    EXPECT_EQ(choice.as<ChoiceTag::Pair>().template get<1>(), "value");

    choice.set<ChoiceTag::Empty>();
    EXPECT_TRUE(choice.is<ChoiceTag::Empty>());
}

TEST(Choice, PreservesReferenceCategoriesAndTriviality) {
    using TrivialChoice =
        rstd::Choice<RSTD_CHOICE_TYPES((ChoiceTag::Empty, void), (ChoiceTag::Number, int))>;
    using TagChoice =
        rstd::Choice<RSTD_CHOICE_TYPES((ChoiceTag::Empty, void), (ChoiceTag::Number, void))>;

    static_assert(
        std::same_as<decltype(rstd::declval<TrivialChoice&>().template as<ChoiceTag::Number>()),
                     int&>);
    static_assert(std::same_as<
                  decltype(rstd::declval<TrivialChoice const&>().template as<ChoiceTag::Number>()),
                  int const&>);
    static_assert(
        std::same_as<decltype(rstd::declval<TrivialChoice&&>().template as<ChoiceTag::Number>()),
                     int&&>);
    static_assert(std::same_as<
                  decltype(rstd::declval<TrivialChoice const&&>().template as<ChoiceTag::Number>()),
                  int const&&>);
    static_assert(std::is_trivially_copy_constructible_v<TrivialChoice>);
    static_assert(std::is_trivially_move_constructible_v<TrivialChoice>);
    static_assert(std::is_trivially_copy_assignable_v<TrivialChoice>);
    static_assert(std::is_trivially_move_assignable_v<TrivialChoice>);
    static_assert(std::is_trivially_destructible_v<TrivialChoice>);
    static_assert(sizeof(TagChoice) == sizeof(rstd::u8));
    static_assert(
        std::same_as<CommaTypeChoice::TypeForTag<ChoiceTag::Pair>, rstd::tuple<int, int>>);
}

TEST(Choice, PropagatesPayloadSpecialMemberCapabilities) {
    enum class Tag
    {
        Value,
        Empty
    };
    using CopyOnlyChoice =
        rstd::Choice<RSTD_CHOICE_TYPES((Tag::Value, CopyOnlyPayload), (Tag::Empty, void))>;
    using MoveOnlyChoice =
        rstd::Choice<RSTD_CHOICE_TYPES((Tag::Value, MoveOnlyPayload), (Tag::Empty, void))>;
    using ImmobileChoice =
        rstd::Choice<RSTD_CHOICE_TYPES((Tag::Value, ImmobilePayload), (Tag::Empty, void))>;
    using NonTrivialDropChoice =
        rstd::Choice<RSTD_CHOICE_TYPES((Tag::Value, NonTrivialDropPayload), (Tag::Empty, void))>;

    static_assert(std::is_copy_constructible_v<CopyOnlyChoice>);
    static_assert(std::is_copy_assignable_v<CopyOnlyChoice>);
    static_assert(! std::is_move_constructible_v<CopyOnlyChoice>);
    static_assert(! std::is_move_assignable_v<CopyOnlyChoice>);

    static_assert(! std::is_copy_constructible_v<MoveOnlyChoice>);
    static_assert(! std::is_copy_assignable_v<MoveOnlyChoice>);
    static_assert(std::is_move_constructible_v<MoveOnlyChoice>);
    static_assert(std::is_move_assignable_v<MoveOnlyChoice>);

    static_assert(! std::is_copy_constructible_v<ImmobileChoice>);
    static_assert(! std::is_copy_assignable_v<ImmobileChoice>);
    static_assert(! std::is_move_constructible_v<ImmobileChoice>);
    static_assert(! std::is_move_assignable_v<ImmobileChoice>);
    static_assert(! std::is_trivially_destructible_v<NonTrivialDropChoice>);

    auto copy_source = CopyOnlyChoice::with<Tag::Value>(4);
    auto copy_target = CopyOnlyChoice::with<Tag::Empty>();
    copy_target      = copy_source;
    EXPECT_EQ(copy_target.as<Tag::Value>().value, 4);

    auto move_source = MoveOnlyChoice::with<Tag::Value>(7);
    auto move_target = MoveOnlyChoice::with<Tag::Empty>();
    move_target      = std::move(move_source);
    EXPECT_EQ(move_target.as<Tag::Value>().value, 7);

    auto immobile = ImmobileChoice::with<Tag::Value>(9);
    EXPECT_EQ(immobile.as<Tag::Value>().value, 9);
}

TEST(Choice, SupportsConstexprConstructionAndSwitching) {
    constexpr auto result = [] {
        auto choice = DirectChoice::with<ChoiceTag::Number>(5);
        choice.set<ChoiceTag::Pair>(8, "ok");
        return choice.as<ChoiceTag::Pair>().template get<0>();
    }();
    static_assert(result == 8);
}

TEST(Choice, OwnsCopyMoveAndDestructionLifecycle) {
    enum class Tag
    {
        Tracked,
        Number
    };
    using Choice =
        rstd::Choice<RSTD_CHOICE_TYPES((Tag::Tracked, TrackedPayload), (Tag::Number, int))>;

    TrackedPayload::live         = 0;
    TrackedPayload::copies       = 0;
    TrackedPayload::moves        = 0;
    TrackedPayload::copy_assigns = 0;
    TrackedPayload::move_assigns = 0;

    {
        auto first = Choice::with<Tag::Tracked>(3);
        EXPECT_EQ(TrackedPayload::live, 1);

        auto copied = first;
        EXPECT_EQ(TrackedPayload::live, 2);
        EXPECT_EQ(TrackedPayload::copies, 1);

        auto moved = std::move(copied);
        EXPECT_EQ(TrackedPayload::live, 3);
        EXPECT_EQ(TrackedPayload::moves, 1);

        moved.set<Tag::Number>(7);
        EXPECT_EQ(TrackedPayload::live, 2);
        EXPECT_EQ(moved.as<Tag::Number>(), 7);
    }

    EXPECT_EQ(TrackedPayload::live, 0);
}

TEST(Choice, AssignsWithinAndAcrossActiveVariants) {
    enum class Tag
    {
        Tracked,
        Number
    };
    using Choice =
        rstd::Choice<RSTD_CHOICE_TYPES((Tag::Tracked, TrackedPayload), (Tag::Number, int))>;

    TrackedPayload::live         = 0;
    TrackedPayload::copies       = 0;
    TrackedPayload::moves        = 0;
    TrackedPayload::copy_assigns = 0;
    TrackedPayload::move_assigns = 0;

    {
        auto source = Choice::with<Tag::Tracked>(3);
        auto same   = Choice::with<Tag::Tracked>(5);
        auto other  = Choice::with<Tag::Number>(8);

        same = source;
        EXPECT_EQ(same.as<Tag::Tracked>().value, 3);
        EXPECT_EQ(TrackedPayload::copy_assigns, 1);

        other = source;
        EXPECT_EQ(other.as<Tag::Tracked>().value, 3);
        EXPECT_EQ(TrackedPayload::copies, 1);

        same = std::move(source);
        EXPECT_EQ(same.as<Tag::Tracked>().value, 3);
        EXPECT_EQ(TrackedPayload::move_assigns, 1);

        auto* source_alias = &source;
        source             = *source_alias;
        EXPECT_TRUE(source.is<Tag::Tracked>());
        source = std::move(*source_alias);
        EXPECT_TRUE(source.is<Tag::Tracked>());
    }

    EXPECT_EQ(TrackedPayload::live, 0);
}

TEST(Choice, RejectsWrongActiveTagAccess) {
    auto choice = DirectChoice::with<ChoiceTag::Number>(3);
    EXPECT_DEATH((void)choice.as<ChoiceTag::Pair>(), "");
}

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
        RSTD_CASE(Quit) {
            result = 0;
        }
        RSTD_CASE(Move, x, y) {
            result = x + y;
        }
        RSTD_CASE_PAYLOAD(Write, payload) {
            result = static_cast<int>(payload.text.size());
        }
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
        RSTD_CASE(Empty) {
        }
        RSTD_CASE(Value, value) {
            taken = std::move(value);
        }
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
    auto        score = InlineScore::Points(6);

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

TEST(Enum, SupportsInheritanceAndFactoryOverloads) {
    auto extended = ExtendedMessage::Value(5);
    EXPECT_EQ(extended.total(), 9);

    auto overloaded = OverloadedFactory::Value('A');
    EXPECT_EQ(overloaded.as_Value().value, 65);
}

TEST(Enum, ReplaceBuildsTemporaryBeforeDestroyingCurrentPayload) {
    static_assert(! rstd::mtp::noex_init<NoThrowMovePayload, FallibleArg>);
    static_assert(rstd::mtp::noex_move<NoThrowMovePayload>);

    {
        enum class Tag
        {
            Value
        };
        using Choice = rstd::Choice<RSTD_CHOICE_TYPES((Tag::Value, NoThrowMovePayload))>;
        auto storage = Choice::with<Tag::Value>(FallibleArg { 1 });

        NoThrowMovePayload::live_payloads_during_create = -1;
        storage.set<Tag::Value>(FallibleArg { 2 });

        EXPECT_TRUE(storage.is<Tag::Value>());
        EXPECT_EQ(storage.as<Tag::Value>().value, 2);
        EXPECT_EQ(NoThrowMovePayload::live_payloads_during_create, 1);
        EXPECT_EQ(NoThrowMovePayload::live_payloads, 1);
    }

    EXPECT_EQ(NoThrowMovePayload::live_payloads, 0);
}
