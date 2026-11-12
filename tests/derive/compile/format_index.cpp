import rstd.core;

auto invalid() {
    rstd::i32 value(1);
    return rstd::fmt::Arguments::make("{1}", value);
}
