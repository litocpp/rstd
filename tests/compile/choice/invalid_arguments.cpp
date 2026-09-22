import rstd.core;

using Choice = rstd::Choice<rstd::choice_case<0, int>>;

auto main() -> int {
    auto value = Choice::with<0>("value");
    return value.as<0>();
}
