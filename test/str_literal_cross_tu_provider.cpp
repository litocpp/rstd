import rstd;

using namespace rstd::literals;

auto rstd_test_cross_tu_str() -> rstd::ref<rstd::str> {
    return "cross-tu"_str;
}

auto rstd_test_cross_tu_bytes() -> rstd::slice<rstd::u8> {
    return "cross-tu"_bytes;
}
