import rstd.core;

using InvalidChoice = rstd::Choice<rstd::choice_case<0, int>, rstd::choice_case<0, long>>;

static_assert(sizeof(InvalidChoice) > 0);

auto main() -> int {
    return 0;
}
