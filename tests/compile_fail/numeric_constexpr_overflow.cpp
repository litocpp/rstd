import rstd;

using namespace rstd::literals;

constexpr auto value = rstd::u8::MAX + 1_u8;

auto main() -> int {
    (void)value;
}
