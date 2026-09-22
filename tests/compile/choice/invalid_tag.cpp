import rstd.core;

using Choice = rstd::Choice<rstd::choice_case<0, int>, rstd::choice_case<1, long>>;

auto main() -> int {
    auto value = Choice::with<3>(1);
    return value.as<3>();
}
