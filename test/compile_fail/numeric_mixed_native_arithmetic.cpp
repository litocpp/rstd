import rstd;

auto main() -> int {
    auto          value  = rstd::u8();
    rstd::uint8_t native = 1;
    (void)(value + native);
}
