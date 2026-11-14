#include <rstd/enum.hpp>
#include <rstd/test/gtest.hpp>

import rstd.tests.enum_macro.fixture;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace enum_macro::fixture;
using alloc::boxed::Box;

namespace enum_macro::matching
{
struct OldEvent {
    RSTD_ENUM(OldEvent, (Unit), (Value, (i32 number;)))
};

[[pmacro::invoke("rstd::Enum",
                 enum NewEvent {
                     Unit,
                     Value(number : i32),
                 })]];

[[pmacro::invoke("rstd::Enum",
                 enum Owned {
                     Unit,
                     Value(Box<i32>),
                 })]];

template<typename Event>
auto read(const Event& event) -> i32 {
    RSTD_MATCH(event) {
        RSTD_CASE(Unit) {
            return i32();
        }
        RSTD_CASE(Value, number) {
            return number;
        }
    }
}

TEST(EnumMacroMatch, UsesTheSameMatchCasesForNewAndOldDeclarations) {
    EXPECT_EQ(read(NewEvent::Unit()), i32());
    EXPECT_EQ(read(OldEvent::Unit()), i32());
    EXPECT_EQ(read(NewEvent::Value(i32(5))), i32(5));
    EXPECT_EQ(read(OldEvent::Value(i32(7))), i32(7));
    i32 result;
    RSTD_MATCH(NewEvent::Value(i32(3))) {
        RSTD_CASE(Unit) {
            result = i32(-1);
        }
        RSTD_CASE(Value, outer) {
            RSTD_MATCH(OldEvent::Value(i32(4))) {
                RSTD_CASE(Unit) {
                    result = i32(-2);
                }
                RSTD_CASE(Value, inner) {
                    result = outer + inner;
                }
            }
        }
    }
    EXPECT_EQ(result, i32(7));
}

TEST(EnumMacroMatch, SharesPayloadAndTagProtocolWithDirectChoice) {
    using Choice = rstd::Choice<rstd::choice_case<NewEvent::Tag::Unit, void>,
                                rstd::choice_case<NewEvent::Tag::Value, NewEvent::Value_payload>>;
    auto choice  = Choice::with<NewEvent::Tag::Value>(i32(4));
    EXPECT_EQ(choice.which(), NewEvent::Value(i32(4)).tag());
    EXPECT_EQ(choice.as<NewEvent::Tag::Value>().number, i32(4));
    choice.set<NewEvent::Tag::Unit>();
    EXPECT_EQ(choice.index(), NewEvent::Unit().index());
}

TEST(EnumMacroMatch, BindsTupleAndNamedFieldsByMutableReference) {
    auto message = Message::Tuple(i32(3), "before"_Str);
    RSTD_MATCH(message) {
        RSTD_CASE(Tuple, number, text) {
            number += i32(2);
            text = "after"_Str;
        }
        RSTD_CASE_DEFAULT {
            FAIL();
        }
    }
    EXPECT_EQ(message.as_Tuple().field_0, i32(5));
    EXPECT_EQ(message.as_Tuple().field_1.as_str(), "after"_str);
    message.replace_Named(i32(7), "named"_Str);
    RSTD_MATCH(message) {
        RSTD_CASE_PAYLOAD(Named, payload) {
            payload.count += i32(1);
        }
        RSTD_CASE_DEFAULT {
            FAIL();
        }
    }
    EXPECT_EQ(message.as_Named().count, i32(8));
}

TEST(EnumMacroMatch, EvaluatesTemporaryOnceAndPreservesLoopControl) {
    int  evaluated = 0;
    int  completed = 0;
    int  total     = 0;
    auto next      = [&](int index) {
        ++evaluated;
        return index == 0 ? NewEvent::Unit() : NewEvent::Value(i32(index));
    };
    for (int index = 0; index < 3; ++index) {
        RSTD_MATCH(next(index)) {
            RSTD_CASE(Unit) {
                continue;
            }
            RSTD_CASE_DEFAULT {
                RSTD_MATCH(OldEvent::Value(i32(index))) {
                    RSTD_CASE(Value, number) {
                        total += number.to_primitive();
                    }
                    RSTD_CASE_DEFAULT {
                        total = -100;
                    }
                }
                break;
            }
        }
        ++completed;
    }
    EXPECT_EQ(evaluated, 3);
    EXPECT_EQ(completed, 2);
    EXPECT_EQ(total, 3);
}

TEST(EnumMacroMatch, MovesPayloadOutOfRvalueMatch) {
    auto value = Owned::Value(Box<i32>::make(i32(42)));
    auto taken = Box<i32>::make(i32());
    RSTD_MATCH(rstd::move(value)) {
        RSTD_CASE(Unit) {
            FAIL();
        }
        RSTD_CASE(Value, number) {
            taken = rstd::move(number);
        }
    }
    EXPECT_EQ(*taken, i32(42));
}

TEST(EnumMacroMatch, MatchesTemplatePayloadAndEmptyPayloads) {
    i32 result;
    RSTD_MATCH(Maybe<i32>::Some(i32(9))) {
        RSTD_CASE(None) {
            result = i32(-1);
        }
        RSTD_CASE(Some, number) {
            result = number;
        }
    }
    EXPECT_EQ(result, i32(9));
    RSTD_MATCH(Message::EmptyTuple()) {
        RSTD_CASE(EmptyTuple) {
            result += i32(1);
        }
        RSTD_CASE_DEFAULT {
            FAIL();
        }
    }
    RSTD_MATCH(Message::EmptyRecord()) {
        RSTD_CASE(EmptyRecord) {
            result += i32(1);
        }
        RSTD_CASE_DEFAULT {
            FAIL();
        }
    }
    EXPECT_EQ(result, i32(11));
}
} // namespace enum_macro::matching
