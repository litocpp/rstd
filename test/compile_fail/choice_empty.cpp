import rstd.core;

using InvalidChoice = rstd::Choice<>;

static_assert(sizeof(InvalidChoice) > 0);

auto main() -> int {
    return 0;
}
