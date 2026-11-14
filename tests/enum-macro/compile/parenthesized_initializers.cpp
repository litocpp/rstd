import rstd.core;

template<class Left, class Right>
constexpr auto sum(Left left, Right right) -> int {
    return left + right;
}

[[pmacro::invoke("rstd::Enum",
                 enum Initializers {
                     [[default]] Value {
                         first: int  = (sum<int, int>(2, 3)),
                         second: int = 7,
                     },
                 })]];

static_assert(Initializers().as_Value().first == 5);
static_assert(Initializers().as_Value().second == 7);
